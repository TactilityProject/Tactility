// SPDX-License-Identifier: Apache-2.0
#include <drivers/cst66xx.h>

#include <cst66xx_module.h>

#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/pointer.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <cstdlib>
#include <utility>

#define TAG "CST66xx"
#define GET_CONFIG(device) (static_cast<const Cst66xxConfig*>((device)->config))

static const TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(20);

// The protocol uses 4-byte register addresses, ported from the vendor's lib/HynTouch/src/hyn_cst66xx.c
// (see Devices/lilygo-tdeck-max's former Source/devices/Cst66xxTouch.cpp for the pre-migration history).

// Normal-mode setup sequence (cst66xx_set_workmode, NOMAL_MODE).
static constexpr uint8_t CMD_DISABLE_LP_I2C[4] = { 0xD0, 0x00, 0x04, 0x00 };
static constexpr uint8_t CMD_NORMAL_A[4] = { 0xD0, 0x00, 0x00, 0x00 };
static constexpr uint8_t CMD_NORMAL_B[4] = { 0xD0, 0x00, 0x0C, 0x00 };
static constexpr uint8_t CMD_NORMAL_C[4] = { 0xD0, 0x00, 0x01, 0x00 };
// Read-point register: write these 4 bytes, then read the touch frame.
static constexpr uint8_t REG_READ_POINT[4] = { 0xD0, 0x07, 0x00, 0x00 };
// Acknowledge/clear after each read.
static constexpr uint8_t CMD_ACK[4] = { 0xD0, 0x00, 0x02, 0xAB };
// Identity/config register (cst66xx_updata_tpinfo): read 50 bytes; buf[2]==0xCA && buf[3]==0xCA confirms a CST66xx.
static constexpr uint8_t REG_INFO[4] = { 0xD0, 0x03, 0x00, 0x00 };

struct Cst66xxInternal {
    int16_t last_x;
    int16_t last_y;
    bool has_point;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
};

// region Driver lifecycle

static void set_normal_mode(Device* i2c, uint8_t address) {
    i2c_controller_write(i2c, address, CMD_DISABLE_LP_I2C, sizeof(CMD_DISABLE_LP_I2C), I2C_TIMEOUT);
    delay_millis(1);
    i2c_controller_write(i2c, address, CMD_DISABLE_LP_I2C, sizeof(CMD_DISABLE_LP_I2C), I2C_TIMEOUT);
    i2c_controller_write(i2c, address, CMD_NORMAL_A, sizeof(CMD_NORMAL_A), I2C_TIMEOUT);
    i2c_controller_write(i2c, address, CMD_NORMAL_B, sizeof(CMD_NORMAL_B), I2C_TIMEOUT);
    i2c_controller_write(i2c, address, CMD_NORMAL_C, sizeof(CMD_NORMAL_C), I2C_TIMEOUT);
}

static void perform_hardware_reset(const Cst66xxConfig* config) {
    if (config->pin_reset.gpio_controller == nullptr) {
        return;
    }

    // Not requesting GPIO_FLAG_ACTIVE_LOW here: some GPIO expanders (e.g. xl9555/tca95xx/
    // tca9534) can only invert what's read back from an input, not what's driven on an
    // output, so they reject ACTIVE_LOW combined with OUTPUT. Drive the raw physical level
    // instead: this pin's reset line is active-low, so low asserts and high releases.
    auto* rst = gpio_descriptor_acquire(config->pin_reset.gpio_controller, config->pin_reset.pin, GPIO_FLAG_DIRECTION_OUTPUT, GPIO_OWNER_GPIO);
    if (rst == nullptr) {
        return;
    }

    gpio_descriptor_set_level(rst, false);
    delay_millis(10);
    gpio_descriptor_set_level(rst, true);
    gpio_descriptor_release(rst);
    // The reset pulse exits boot mode; give the controller time to re-init.
    delay_millis(80);
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Cst66xxInternal*>(malloc(sizeof(Cst66xxInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->last_x = 0;
    internal->last_y = 0;
    internal->has_point = false;
    internal->swap_xy = config->swap_xy;
    internal->mirror_x = config->mirror_x;
    internal->mirror_y = config->mirror_y;

    perform_hardware_reset(config);

    // Put the controller into normal reporting mode and verify the identity. The first read
    // after reset can miss, so retry like the vendor's cst66xx_updata_tpinfo (read 0xD0030000,
    // expect buf[2]==buf[3]==0xCA).
    bool confirmed = false;
    for (int attempt = 0; attempt < 5 && !confirmed; ++attempt) {
        set_normal_mode(parent, config->address);
        uint8_t info[50] = {};
        if (i2c_controller_write(parent, config->address, REG_INFO, sizeof(REG_INFO), I2C_TIMEOUT) == ERROR_NONE &&
            i2c_controller_read(parent, config->address, info, sizeof(info), I2C_TIMEOUT) == ERROR_NONE &&
            info[2] == 0xCA && info[3] == 0xCA) {
            confirmed = true;
        } else {
            delay_millis(10);
        }
    }
    if (confirmed) {
        LOG_I(TAG, "CST66xx initialised");
    } else {
        LOG_W(TAG, "CST66xx identity not confirmed (continuing)");
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Cst66xxInternal*>(device_get_driver_data(device));
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region PointerApi

static error_t cst66xx_read_data(Device* device, TickType_t /*timeout*/) {
    auto* internal = static_cast<Cst66xxInternal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);
    auto* parent = device_get_parent(device);

    // CST66xx frame (cst66xx_report): write the read-point register, then read 9 bytes.
    // buf[2]=report type (0xFF=position/key), buf[3] low nibble = finger count, high nibble =
    // key count. The first 5-byte slot (buf[4..8]) is a key slot when key count > 0, otherwise
    // the first finger; further fingers, if any, follow at buf[index+...].
    uint8_t buf[9] = {};
    error_t err = i2c_controller_write(parent, config->address, REG_READ_POINT, sizeof(REG_READ_POINT), I2C_TIMEOUT);
    if (err == ERROR_NONE) {
        err = i2c_controller_read(parent, config->address, buf, sizeof(buf), I2C_TIMEOUT);
    }
    if (err != ERROR_NONE) {
        return ERROR_RESOURCE;
    }

    // Acknowledge/clear the frame after every read.
    i2c_controller_write(parent, config->address, CMD_ACK, sizeof(CMD_ACK), I2C_TIMEOUT);

    const uint8_t reportType = buf[2];
    const uint8_t fingerCount = buf[3] & 0x0F;
    const uint8_t keyCount = (buf[3] & 0xF0) >> 4;

    // The controller also reports the three bezel touch-keys (heart/speech-bubble/paper-airplane)
    // mutually exclusively with finger coordinates in this same frame shape. Kernel drivers don't
    // have a home for UI-navigation policy (see the CST66xx driver's module README), so key frames
    // are just treated as "no touch" here.
    if ((reportType == 0xFF && keyCount > 0) || reportType != 0xFF || fingerCount < 1) {
        internal->has_point = false;
        return ERROR_NONE;
    }

    // First finger's slot follows any key slots.
    const uint8_t index = keyCount * 5;
    const uint8_t event = buf[index + 8] >> 4; // 0 = up
    if (event == 0) {
        internal->has_point = false;
        return ERROR_NONE;
    }

    int16_t rawX = static_cast<int16_t>(buf[index + 4] | (static_cast<uint16_t>(buf[index + 7] & 0x0F) << 8));
    int16_t rawY = static_cast<int16_t>(buf[index + 5] | (static_cast<uint16_t>(buf[index + 7] & 0xF0) << 4));

    if (internal->swap_xy) {
        std::swap(rawX, rawY);
    }
    if (internal->mirror_x) {
        rawX = static_cast<int16_t>(config->x_max - 1 - rawX);
    }
    if (internal->mirror_y) {
        rawY = static_cast<int16_t>(config->y_max - 1 - rawY);
    }

    internal->last_x = rawX;
    internal->last_y = rawY;
    internal->has_point = true;
    return ERROR_NONE;
}

static bool cst66xx_get_touched_points(Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    auto* internal = static_cast<Cst66xxInternal*>(device_get_driver_data(device));
    if (!internal->has_point || max_point_count < 1) {
        *point_count = 0;
        return false;
    }

    x[0] = static_cast<uint16_t>(internal->last_x);
    y[0] = static_cast<uint16_t>(internal->last_y);
    if (strength != nullptr) {
        strength[0] = 0;
    }
    *point_count = 1;
    return true;
}

static error_t cst66xx_set_swap_xy(Device* device, bool swap) {
    static_cast<Cst66xxInternal*>(device_get_driver_data(device))->swap_xy = swap;
    return ERROR_NONE;
}

static error_t cst66xx_get_swap_xy(Device* device, bool* swap) {
    *swap = static_cast<Cst66xxInternal*>(device_get_driver_data(device))->swap_xy;
    return ERROR_NONE;
}

static error_t cst66xx_set_mirror_x(Device* device, bool mirror) {
    static_cast<Cst66xxInternal*>(device_get_driver_data(device))->mirror_x = mirror;
    return ERROR_NONE;
}

static error_t cst66xx_get_mirror_x(Device* device, bool* mirror) {
    *mirror = static_cast<Cst66xxInternal*>(device_get_driver_data(device))->mirror_x;
    return ERROR_NONE;
}

static error_t cst66xx_set_mirror_y(Device* device, bool mirror) {
    static_cast<Cst66xxInternal*>(device_get_driver_data(device))->mirror_y = mirror;
    return ERROR_NONE;
}

static error_t cst66xx_get_mirror_y(Device* device, bool* mirror) {
    *mirror = static_cast<Cst66xxInternal*>(device_get_driver_data(device))->mirror_y;
    return ERROR_NONE;
}

// enter_sleep/exit_sleep are not exposed: the ported protocol has no equivalent commands.

// endregion

static const PointerApi cst66xx_pointer_api = {
    .enter_sleep = nullptr,
    .exit_sleep = nullptr,
    .read_data = cst66xx_read_data,
    .get_touched_points = cst66xx_get_touched_points,
    .set_swap_xy = cst66xx_set_swap_xy,
    .get_swap_xy = cst66xx_get_swap_xy,
    .set_mirror_x = cst66xx_set_mirror_x,
    .get_mirror_x = cst66xx_get_mirror_x,
    .set_mirror_y = cst66xx_set_mirror_y,
    .get_mirror_y = cst66xx_get_mirror_y,
};

Driver cst66xx_driver = {
    .name = "cst66xx",
    .compatible = (const char*[]) { "hynitron,cst66xx", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &cst66xx_pointer_api,
    .device_type = &POINTER_TYPE,
    .owner = &cst66xx_module,
    .internal = nullptr
};
