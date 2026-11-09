// SPDX-License-Identifier: Apache-2.0
#include <drivers/bmm150.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/compass.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#include <cstring>
#include <limits>
#include <new>

#define TAG "BMM150"
#define GET_CONFIG(device) (static_cast<const Bmm150Config*>((device)->config))

namespace {

// Register map (Bosch BMM150 datasheet)
constexpr uint8_t REG_CHIP_ID = 0x40;
constexpr uint8_t CHIP_ID_VALUE = 0x32;
constexpr uint8_t REG_DATA_X_LSB = 0x42; // 8 bytes: X/Y/Z LSB+MSB, RHALL LSB+MSB
constexpr uint8_t REG_POWER = 0x4B;      // bit0: 1 = sleep/active, 0 = suspend
constexpr uint8_t REG_OPMODE_ODR = 0x4C; // bits 2:1 = opmode, bits 5:3 = ODR
constexpr uint8_t REG_REP_XY = 0x51;
constexpr uint8_t REG_REP_Z = 0x52;
constexpr uint8_t REG_TRIM_START = 0x5D;
constexpr size_t TRIM_LENGTH = 21; // 0x5D..0x71

constexpr uint8_t MASK_POWER_CTL = 1u << 0;
constexpr uint8_t MASK_SOFT_RESET = (1u << 7) | (1u << 1);
constexpr uint8_t MASK_OPMODE = (1u << 2) | (1u << 1);
constexpr uint8_t SHIFT_OPMODE = 1;
constexpr uint8_t MASK_ODR = (1u << 5) | (1u << 4) | (1u << 3);
constexpr uint8_t SHIFT_ODR = 3;
constexpr uint8_t MODE_NORMAL = 0x00;

constexpr int16_t XY_OVERFLOW = -4096;
constexpr int16_t Z_OVERFLOW = -16384;

// Regular preset: 10 Hz ODR, nXY = 9, nZ = 15
constexpr uint8_t ODR_10HZ = 0x00;
constexpr uint8_t REP_XY_REG = (9 - 1) / 2;
constexpr uint8_t REP_Z_REG = (15 - 1) / 2;

// Field is reported in 16 LSB/µT.
constexpr float MICROTESLA_PER_LSB = 1.0f / 16.0f;

constexpr TickType_t I2C_TIMEOUT_TICKS = pdMS_TO_TICKS(10);

struct TrimRegs {
    int8_t x1;
    int8_t y1;
    int16_t z4;
    int8_t x2;
    int8_t y2;
    int16_t z2;
    uint16_t z1;
    uint16_t xyz1;
    int16_t z3;
    int8_t xy2;
    uint8_t xy1;
};

struct Bmm150Internal {
    Device* i2c_controller;
    uint8_t address;
    TrimRegs trim;
};

// region I2C helpers

bool reg_read(Device* controller, uint8_t address, uint8_t reg, uint8_t* data, size_t size) {
    return i2c_controller_read_register(controller, address, reg, data, size, I2C_TIMEOUT_TICKS) == ERROR_NONE;
}

bool reg_write(Device* controller, uint8_t address, uint8_t reg, uint8_t value) {
    return i2c_controller_register8_set(controller, address, reg, value, I2C_TIMEOUT_TICKS) == ERROR_NONE;
}

// endregion

// region Compensation
// Ported from the BMM150 compensation equations (Bosch datasheet 4.3.2), matching the reference
// implementation used by Zephyr's bmm150 driver (Apache-2.0).

int32_t compensate_xy(const TrimRegs& trim, int16_t xy, uint16_t rhall, bool is_x) {
    if (xy == XY_OVERFLOW) {
        return std::numeric_limits<int32_t>::min();
    }
    if (rhall == 0) {
        rhall = trim.xyz1;
    }

    const int8_t txy1 = is_x ? trim.x1 : trim.y1;
    const int8_t txy2 = is_x ? trim.x2 : trim.y2;

    const uint16_t prevalue = static_cast<uint16_t>((static_cast<int32_t>(trim.xyz1) << 14) / rhall);
    int16_t val = static_cast<int16_t>(static_cast<uint16_t>(prevalue) - static_cast<uint16_t>(0x4000));

    const int32_t temp1 = static_cast<int32_t>(trim.xy2) * ((static_cast<int32_t>(val) * static_cast<int32_t>(val)) >> 7);
    const int32_t temp2 = static_cast<int32_t>(val) * static_cast<int32_t>(static_cast<int16_t>(trim.xy1) << 7);
    const int32_t temp3 = ((((temp1 + temp2) >> 9) + static_cast<int32_t>(0x100000)) *
        static_cast<int32_t>(static_cast<int16_t>(txy2) + static_cast<int16_t>(0xA0))) >> 12;

    val = static_cast<int16_t>((static_cast<int32_t>(xy) * temp3) >> 13) +
        (static_cast<int16_t>(txy1) << 3);
    return val;
}

int32_t compensate_z(const TrimRegs& trim, int16_t z, uint16_t rhall) {
    if (z == Z_OVERFLOW) {
        return std::numeric_limits<int32_t>::min();
    }

    const int32_t temp1 = static_cast<int32_t>(z - trim.z4) << 15;
    const int32_t temp2 = (static_cast<int32_t>(trim.z3) *
        (static_cast<int32_t>(static_cast<int16_t>(rhall) - static_cast<int16_t>(trim.xyz1)))) >> 2;
    const int16_t temp3 = static_cast<int16_t>(
        ((static_cast<int32_t>(trim.z1) * (static_cast<int16_t>(rhall) << 1)) + (1 << 15)) >> 16);
    return (temp1 - temp2) / (trim.z2 + temp3);
}

// endregion

bool read_trim_registers(Bmm150Internal* internal) {
    uint8_t raw[TRIM_LENGTH] = {};
    if (!reg_read(internal->i2c_controller, internal->address, REG_TRIM_START, raw, sizeof(raw))) {
        return false;
    }

    auto le16 = [](const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); };
    auto le16s = [&](const uint8_t* p) { return static_cast<int16_t>(le16(p)); };

    internal->trim = {
        .x1 = static_cast<int8_t>(raw[0x5D - REG_TRIM_START]),
        .y1 = static_cast<int8_t>(raw[0x5E - REG_TRIM_START]),
        .z4 = le16s(&raw[0x62 - REG_TRIM_START]),
        .x2 = static_cast<int8_t>(raw[0x64 - REG_TRIM_START]),
        .y2 = static_cast<int8_t>(raw[0x65 - REG_TRIM_START]),
        .z2 = le16s(&raw[0x68 - REG_TRIM_START]),
        .z1 = le16(&raw[0x6A - REG_TRIM_START]),
        .xyz1 = le16(&raw[0x6C - REG_TRIM_START]),
        .z3 = le16s(&raw[0x6E - REG_TRIM_START]),
        .xy2 = static_cast<int8_t>(raw[0x70 - REG_TRIM_START]),
        .xy1 = raw[0x71 - REG_TRIM_START],
    };
    return true;
}

bool set_power_control(Bmm150Internal* internal, bool enable) {
    uint8_t value = 0;
    if (!reg_read(internal->i2c_controller, internal->address, REG_POWER, &value, 1)) {
        return false;
    }
    value = static_cast<uint8_t>((value & ~MASK_POWER_CTL) | (enable ? MASK_POWER_CTL : 0));
    return reg_write(internal->i2c_controller, internal->address, REG_POWER, value);
}

bool update_opmode(Bmm150Internal* internal, uint8_t mode) {
    uint8_t value = 0;
    if (!reg_read(internal->i2c_controller, internal->address, REG_OPMODE_ODR, &value, 1)) {
        return false;
    }
    value = static_cast<uint8_t>((value & ~MASK_OPMODE) | ((mode << SHIFT_OPMODE) & MASK_OPMODE));
    return reg_write(internal->i2c_controller, internal->address, REG_OPMODE_ODR, value);
}

bool is_ready(Device* controller, uint8_t address) {
    uint8_t chip_id = 0;
    return reg_read(controller, address, REG_CHIP_ID, &chip_id, 1) && chip_id == CHIP_ID_VALUE;
}

// Full power-on reset, then leave the chip in sleep mode.
bool power_on_reset(Bmm150Internal* internal) {
    if (!set_power_control(internal, true)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(3));

    uint8_t value = 0;
    if (!reg_read(internal->i2c_controller, internal->address, REG_POWER, &value, 1)) {
        return false;
    }
    value |= MASK_SOFT_RESET;
    if (!reg_write(internal->i2c_controller, internal->address, REG_POWER, value)) {
        return false;
    }

    // Datasheet 5.6: full POR requires suspend then back to sleep
    if (!set_power_control(internal, false)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
    if (!set_power_control(internal, true)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(3));
    return true;
}

// region Driver lifecycle

error_t start(Device* device) {
    Device* controller = device_get_parent(device);
    if (controller == nullptr || device_get_type(controller) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    const auto* config = GET_CONFIG(device);

    auto* internal = new(std::nothrow) Bmm150Internal { .i2c_controller = controller, .address = config->address, .trim = {} };
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    if (!power_on_reset(internal) || !is_ready(controller, config->address)) {
        LOG_E(TAG, "0x%02X: chip ID mismatch (not a BMM150?)", config->address);
        delete internal;
        return ERROR_RESOURCE;
    }

    // Regular preset: 10 Hz, nXY = 9, nZ = 15
    uint8_t opmode = 0;
    if (!reg_read(controller, config->address, REG_OPMODE_ODR, &opmode, 1)) {
        LOG_E(TAG, "0x%02X: failed to read OPMODE", config->address);
        delete internal;
        return ERROR_RESOURCE;
    }
    opmode = static_cast<uint8_t>((opmode & ~MASK_ODR) | ((ODR_10HZ << SHIFT_ODR) & MASK_ODR));
    if (!reg_write(controller, config->address, REG_OPMODE_ODR, opmode) ||
        !reg_write(controller, config->address, REG_REP_XY, REP_XY_REG) ||
        !reg_write(controller, config->address, REG_REP_Z, REP_Z_REG) ||
        !update_opmode(internal, MODE_NORMAL)) {
        LOG_E(TAG, "0x%02X: failed to configure measurement", config->address);
        delete internal;
        return ERROR_RESOURCE;
    }

    if (!read_trim_registers(internal)) {
        LOG_E(TAG, "0x%02X: failed to read trim registers", config->address);
        delete internal;
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);

    LOG_I(TAG, "0x%02X: initialized", config->address);

    return ERROR_NONE;
}

error_t stop(Device* device) {
    auto* internal = static_cast<Bmm150Internal*>(device_get_driver_data(device));
    if (internal != nullptr) {
        set_power_control(internal, false); // Suspend
        delete internal;
        device_set_driver_data(device, nullptr);
    }
    return ERROR_NONE;
}

// endregion

// region CompassApi

error_t read_field(Device* device, CompassData* data) {
    auto* internal = static_cast<Bmm150Internal*>(device_get_driver_data(device));
    if (internal == nullptr || data == nullptr) {
        return ERROR_INVALID_STATE;
    }

    uint8_t raw[8] = {};
    if (!reg_read(internal->i2c_controller, internal->address, REG_DATA_X_LSB, raw, sizeof(raw))) {
        return ERROR_RESOURCE;
    }

    auto le16 = [](const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); };

    const int16_t raw_x = static_cast<int16_t>(static_cast<int16_t>(le16(&raw[0])) >> 3);
    const int16_t raw_y = static_cast<int16_t>(static_cast<int16_t>(le16(&raw[2])) >> 3);
    const int16_t raw_z = static_cast<int16_t>(static_cast<int16_t>(le16(&raw[4])) >> 1);
    const uint16_t rhall = static_cast<uint16_t>(le16(&raw[6]) >> 2);

    const int32_t comp_x = compensate_xy(internal->trim, raw_x, rhall, true);
    const int32_t comp_y = compensate_xy(internal->trim, raw_y, rhall, false);
    const int32_t comp_z = compensate_z(internal->trim, raw_z, rhall);
    if (comp_x == std::numeric_limits<int32_t>::min() ||
        comp_y == std::numeric_limits<int32_t>::min() ||
        comp_z == std::numeric_limits<int32_t>::min()) {
        return ERROR_RESOURCE; // sensor overflow
    }

    float x = static_cast<float>(comp_x) * MICROTESLA_PER_LSB;
    float y = static_cast<float>(comp_y) * MICROTESLA_PER_LSB;
    float z = static_cast<float>(comp_z) * MICROTESLA_PER_LSB;

    const int32_t* m = GET_CONFIG(device)->mount_matrix;
    data->x = m[0] * x + m[1] * y + m[2] * z;
    data->y = m[3] * x + m[4] * y + m[5] * z;
    data->z = m[6] * x + m[7] * y + m[8] * z;
    return ERROR_NONE;
}

// endregion

const CompassApi bmm150_compass_api = {
    .read_field = read_field,
    .read_heading = nullptr,
};

} // namespace

extern "C" error_t bmm150_read_field(Device* device, CompassData* data) {
    return read_field(device, data);
}

extern "C" {

Driver bmm150_driver = {
    .name = "bmm150",
    .compatible = (const char*[]) { "bosch,bmm150", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &bmm150_compass_api,
    .device_type = &COMPASS_TYPE,
    .internal = nullptr
};

}
