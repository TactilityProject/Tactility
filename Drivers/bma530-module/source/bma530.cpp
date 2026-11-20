// SPDX-License-Identifier: Apache-2.0
#include <drivers/bma530.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#include <new>

#define TAG "BMA530"
#define GET_CONFIG(device) (static_cast<const Bma530Config*>((device)->config))
#define GET_INTERNAL(device) (static_cast<Bma530Internal*>(device_get_driver_data(device)))

// Register map based on the BMA530 datasheet
static constexpr uint8_t REG_CHIP_ID = 0x00; // read: expect 0xC2, the first read after power-up NACKs
static constexpr uint8_t REG_CMD_SUSPEND = 0x04; // bit0: enter suspend
static constexpr uint8_t REG_SENSOR_STATUS = 0x11; // bit0 acc drdy, bit1 temp drdy, bit2 sensor ready (write 1 to clear)
static constexpr uint8_t REG_INT_STATUS_INT1 = 0x12; // 2 bytes per line: INT1, INT2, I3C
static constexpr uint8_t REG_ACC_DATA = 0x18; // 6 bytes: acc X/Y/Z, LSB first
static constexpr uint8_t REG_TEMP_DATA = 0x1E; // 1 byte, 1 K/LSB, 0 = 23 degrees C
static constexpr uint8_t REG_SENSOR_TIME = 0x1F; // 3 bytes, LSB first
static constexpr uint8_t REG_ACC_CONF_0 = 0x30; // sensor enable
static constexpr uint8_t REG_ACC_CONF_1 = 0x31; // ODR, bandwidth, power mode; followed by ACC_CONF_2
static constexpr uint8_t REG_TEMP_CONF = 0x33; // temperature sample rate
static constexpr uint8_t REG_INT_CONF_1 = 0x34; // INT1 pin electrical config; INT2 follows
static constexpr uint8_t REG_INT_MAP_0 = 0x36; // 4 bytes, 2 bits per interrupt source
static constexpr uint8_t REG_GPR_CTRL = 0x54; // bit0: commit host-owned feature engine registers
static constexpr uint8_t REG_GPR_0 = 0x55; // feature enable bits
static constexpr uint8_t REG_GPR_1 = 0x56; // 5 bytes of feature engine output
static constexpr uint8_t REG_FEATURE_DATA_ADDR = 0x5E; // feature engine configuration window: base address
static constexpr uint8_t REG_FEATURE_DATA_TX = 0x5F; // feature engine configuration window: data
static constexpr uint8_t REG_ACC_OFFSET = 0x70; // 6 bytes: per axis a low byte and a bit-8 byte
static constexpr uint8_t REG_CMD = 0x7E; // 0xB6 = soft reset, the sensor does not ACK it

static constexpr uint8_t CHIP_ID = 0xC2;
static constexpr uint8_t CMD_SOFT_RESET = 0xB6;
static constexpr uint8_t SENSOR_CTRL_ENABLED = 0x0F;
static constexpr uint8_t SENSOR_CTRL_DISABLED = 0x00;
static constexpr uint8_t GPR_CTRL_UPDATE = 0x01;
static constexpr uint8_t ORIENTATION_BASE_ADDRESS = 0x30;

// Reads through the feature data window are preceded by two padding bytes (interesting...)
static constexpr size_t FEATURE_DATA_PADDING = 2;
static constexpr size_t ORIENTATION_CONFIG_SIZE = 4;
static constexpr size_t FEATURE_OUTPUT_SIZE = 5;

static constexpr uint8_t SENSOR_STATUS_ACC_DRDY = 0x01;
static constexpr uint8_t SENSOR_STATUS_TEMP_DRDY = 0x02;
static constexpr uint8_t SENSOR_STATUS_SENSOR_RDY = 0x04;

static constexpr uint8_t ODR_MAX = BMA530_ODR_6400_HZ;
static constexpr uint8_t RANGE_MAX = BMA530_RANGE_16G;
static constexpr uint8_t BANDWIDTH_MAX = BMA530_BWP_CIC_AVG8;
static constexpr uint8_t IIR_ROLL_OFF_MAX = BMA530_IIR_RO_60_DB;
static constexpr uint8_t TEMPERATURE_RATE_MAX = BMA530_TEMPERATURE_RATE_200_HZ;
static constexpr uint8_t INT_MODE_MAX = BMA530_INT_MODE_PULSED_LONG;

static constexpr uint8_t ORIENTATION_MODE_MAX = 2;
static constexpr uint8_t ORIENTATION_BLOCKING_MAX = 3;
static constexpr uint8_t ORIENTATION_THETA_MAX = 0x3F;
static constexpr uint8_t ORIENTATION_HOLD_TIME_MAX = 0x1F;

static constexpr int32_t TEMPERATURE_REFERENCE_C = 23;
static constexpr int16_t OFFSET_SIGN_BIT = 0x100;
static constexpr int16_t OFFSET_RANGE = 0x200;

static constexpr uint32_t RESET_DELAY_MS = 20;
static constexpr TickType_t I2C_TIMEOUT_TICKS = pdMS_TO_TICKS(10);

// Measurement range in g, indexed by Bma530Range. The 16-bit output spans +-range.
static constexpr float RANGE_G[] = { 2.0f, 4.0f, 8.0f, 16.0f };
static constexpr float COUNTS_PER_HALF_SCALE = 32768.0f;

struct InterruptSourceField {
    uint8_t reg_offset;
    uint8_t shift;
};

// Position of each source's 2-bit routing field in INT_MAP_0..3
static constexpr InterruptSourceField INTERRUPT_SOURCE_FIELDS[] = {
    { 0, 0 }, // ACC_DRDY
    { 0, 2 }, // FIFO_WATERMARK
    { 0, 4 }, // FIFO_FULL
    { 0, 6 }, // GENERIC_1
    { 1, 0 }, // GENERIC_2
    { 1, 2 }, // GENERIC_3
    { 1, 4 }, // STEP_DETECTOR
    { 1, 6 }, // STEP_COUNTER
    { 2, 0 }, // SIGNIFICANT_MOTION
    { 2, 2 }, // TILT
    { 2, 4 }, // ORIENTATION
    { 2, 6 }, // ACC_FOC
    { 3, 0 }  // FEATURE_ENGINE_ERROR
};
static constexpr uint8_t INTERRUPT_FIELD_MASK = 0x03;

// Defaults for a battery powered device polled through ImuApi (duty-cycled 50 Hz with the 4 g range)
static constexpr Bma530AccConfig DEFAULT_ACC_CONFIG = {
    .odr = BMA530_ODR_50_HZ,
    .bandwidth = BMA530_BWP_NORM_AVG4,
    .power_mode = BMA530_POWER_MODE_LOW_POWER,
    .range = BMA530_RANGE_4G,
    .iir_roll_off = BMA530_IIR_RO_UNSPECIFIED,
    .noise_mode = BMA530_NOISE_MODE_LOWER_NOISE,
    .drdy_auto_clear = false
};

struct Bma530Internal {
    Mutex mutex;
    // Range currently programmed in the sensor, used to scale readings
    uint8_t range = DEFAULT_ACC_CONFIG.range;

    Bma530Internal() { mutex_construct(&mutex); }
    ~Bma530Internal() { mutex_destruct(&mutex); }
};

class ScopedLock {
    Mutex* mutex;
public:
    explicit ScopedLock(Bma530Internal* internal) : mutex(&internal->mutex) { mutex_lock(mutex); }
    ~ScopedLock() { mutex_unlock(mutex); }
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
};

// region Helpers

static error_t read_registers(Device* device, uint8_t reg, uint8_t* data, size_t size) {
    return i2c_controller_read_register(device_get_parent(device), GET_CONFIG(device)->address, reg, data, size, I2C_TIMEOUT_TICKS);
}

static error_t write_registers(Device* device, uint8_t reg, const uint8_t* data, uint16_t size) {
    return i2c_controller_write_register(device_get_parent(device), GET_CONFIG(device)->address, reg, data, size, I2C_TIMEOUT_TICKS);
}

static error_t read_register(Device* device, uint8_t reg, uint8_t* value) {
    return i2c_controller_register8_get(device_get_parent(device), GET_CONFIG(device)->address, reg, value, I2C_TIMEOUT_TICKS);
}

static error_t write_register(Device* device, uint8_t reg, uint8_t value) {
    return i2c_controller_register8_set(device_get_parent(device), GET_CONFIG(device)->address, reg, value, I2C_TIMEOUT_TICKS);
}

static int16_t to_int16(uint8_t low, uint8_t high) {
    return static_cast<int16_t>(static_cast<uint16_t>(high) << 8 | low);
}

static error_t verify_chip_id(Device* device) {
    uint8_t chip_id = 0;
    // The first access after power-up or reset is NACKed by design(?), so its result is not meaningful
    (void)read_register(device, REG_CHIP_ID, &chip_id);
    error_t error = read_register(device, REG_CHIP_ID, &chip_id);
    if (error != ERROR_NONE) return error;
    if (chip_id != CHIP_ID) {
        LOG_E(TAG, "Unexpected chip id 0x%02X", chip_id);
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

static error_t validate_acc_config(const Bma530AccConfig& config) {
    if (config.odr > ODR_MAX || config.range > RANGE_MAX || config.bandwidth > BANDWIDTH_MAX ||
        config.iir_roll_off > IIR_ROLL_OFF_MAX || config.power_mode > BMA530_POWER_MODE_HIGH_PERFORMANCE ||
        config.noise_mode > BMA530_NOISE_MODE_LOWER_POWER) {
        return ERROR_INVALID_ARGUMENT;
    }
    // ODRs below 12.5 Hz only exist in low power mode, ODRs above 400 Hz only in high performance mode
    if (config.power_mode == BMA530_POWER_MODE_LOW_POWER && config.odr > BMA530_ODR_400_HZ) return ERROR_INVALID_ARGUMENT;
    if (config.power_mode == BMA530_POWER_MODE_HIGH_PERFORMANCE && config.odr < BMA530_ODR_12_5_HZ) return ERROR_INVALID_ARGUMENT;
    return ERROR_NONE;
}

// The sensor has to be disabled while ACC_CONF is rewritten. The caller holds the lock.
static error_t write_acc_config(Device* device, Bma530Internal* internal, const Bma530AccConfig& config) {
    error_t error = validate_acc_config(config);
    if (error != ERROR_NONE) return error;

    error = write_register(device, REG_ACC_CONF_0, SENSOR_CTRL_DISABLED);
    if (error != ERROR_NONE) return error;

    const uint8_t buffer[2] = {
        static_cast<uint8_t>(config.odr | (config.bandwidth << 4) | (config.power_mode << 7)),
        static_cast<uint8_t>(config.range | (config.iir_roll_off << 2) | (config.noise_mode << 4) | (config.drdy_auto_clear ? 0x80 : 0x00))
    };
    error = write_registers(device, REG_ACC_CONF_1, buffer, sizeof(buffer));
    if (error != ERROR_NONE) return error;

    error = write_register(device, REG_ACC_CONF_0, SENSOR_CTRL_ENABLED);
    if (error != ERROR_NONE) return error;

    // The sensor refuses to enable itself with an invalid configuration and reads back a different value, smart!
    uint8_t sensor_ctrl = 0;
    error = read_register(device, REG_ACC_CONF_0, &sensor_ctrl);
    if (error != ERROR_NONE) return error;
    if (sensor_ctrl != SENSOR_CTRL_ENABLED) {
        LOG_E(TAG, "Sensor rejected configuration (ACC_CONF_0=0x%02X)", sensor_ctrl);
        return ERROR_INVALID_ARGUMENT;
    }

    internal->range = config.range;
    return ERROR_NONE;
}

static error_t reset_to_defaults(Device* device, Bma530Internal* internal) {
    // Only CHIP_ID and CMD_SUSPEND respond while suspended, so leave suspend before resetting
    error_t error = write_register(device, REG_CMD_SUSPEND, 0x00);
    if (error != ERROR_NONE) return error;

    // The sensor does not ACK the reset command, so a bus error here is expected .
    // Success is decided by the chip ID check after the reset delay.
    (void)write_register(device, REG_CMD, CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(RESET_DELAY_MS));

    error = verify_chip_id(device);
    if (error != ERROR_NONE) return error;

    return write_acc_config(device, internal, DEFAULT_ACC_CONFIG);
}

// Applies the devicetree mount matrix (sensor frame -> device frame) to a 3-axis reading
// Flat row-major [9]: x' = m0*x + m1*y + m2*z, y' = m3*x + m4*y + m5*z, z' = m6*x + m7*y + m8*z.
static void apply_mount_matrix(const Bma530Config& config, float& x, float& y, float& z) {
    const int32_t* m = config.mount_matrix;
    const float rx = m[0] * x + m[1] * y + m[2] * z;
    const float ry = m[3] * x + m[4] * y + m[5] * z;
    const float rz = m[6] * x + m[7] * y + m[8] * z;
    x = rx;
    y = ry;
    z = rz;
}

static error_t set_feature_window_address(Device* device, uint8_t base_address) {
    return write_register(device, REG_FEATURE_DATA_ADDR, base_address);
}

static uint8_t interrupt_status_register(Bma530IntLine line) {
    return REG_INT_STATUS_INT1 + static_cast<uint8_t>((line - BMA530_INT_LINE_INT1) * 2);
}

static bool is_status_line(Bma530IntLine line) {
    return line == BMA530_INT_LINE_INT1 || line == BMA530_INT_LINE_INT2 || line == BMA530_INT_LINE_I3C;
}

// endregion

// region Driver lifecycle

static error_t start(Device* device) {
    auto* i2c_controller = device_get_parent(device);
    if (device_get_type(i2c_controller) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    error_t error = verify_chip_id(device);
    if (error != ERROR_NONE) return ERROR_RESOURCE;

    auto* internal = new (std::nothrow) Bma530Internal();
    if (internal == nullptr) return ERROR_OUT_OF_MEMORY;

    error = reset_to_defaults(device, internal);
    if (error != ERROR_NONE) {
        delete internal;
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);

    error_t error = write_register(device, REG_ACC_CONF_0, SENSOR_CTRL_DISABLED);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to disable the sensor");
    }

    device_set_driver_data(device, nullptr);
    delete internal;
    return error == ERROR_NONE ? ERROR_NONE : ERROR_RESOURCE;
}

// endregion

extern "C" {

error_t bma530_read_accel_raw(Device* device, int16_t raw[3]) {
    uint8_t buffer[6] = {};
    error_t error = read_registers(device, REG_ACC_DATA, buffer, sizeof(buffer));
    if (error != ERROR_NONE) return error;

    raw[0] = to_int16(buffer[0], buffer[1]);
    raw[1] = to_int16(buffer[2], buffer[3]);
    raw[2] = to_int16(buffer[4], buffer[5]);
    return ERROR_NONE;
}

error_t bma530_read_accel(Device* device, ImuAccelData* data) {
    auto* internal = GET_INTERNAL(device);

    int16_t raw[3] = {};
    float scale = 0.0f;
    {
        ScopedLock lock(internal);
        error_t error = bma530_read_accel_raw(device, raw);
        if (error != ERROR_NONE) return error;
        scale = RANGE_G[internal->range] / COUNTS_PER_HALF_SCALE;
    }

    float x = raw[0] * scale;
    float y = raw[1] * scale;
    float z = raw[2] * scale;
    apply_mount_matrix(*GET_CONFIG(device), x, y, z);
    data->ax = x;
    data->ay = y;
    data->az = z;
    return ERROR_NONE;
}

error_t bma530_read_temperature(Device* device, float* temperature_c) {
    uint8_t raw = 0;
    error_t error = read_register(device, REG_TEMP_DATA, &raw);
    if (error != ERROR_NONE) return error;

    // Two's complement offset from 23 degrees C at 1 K per step
    *temperature_c = static_cast<float>(static_cast<int8_t>(raw) + TEMPERATURE_REFERENCE_C);
    return ERROR_NONE;
}

error_t bma530_read_sensor_time(Device* device, uint32_t* ticks) {
    uint8_t buffer[3] = {};
    error_t error = read_registers(device, REG_SENSOR_TIME, buffer, sizeof(buffer));
    if (error != ERROR_NONE) return error;

    *ticks = static_cast<uint32_t>(buffer[0]) | static_cast<uint32_t>(buffer[1]) << 8 | static_cast<uint32_t>(buffer[2]) << 16;
    return ERROR_NONE;
}

error_t bma530_get_acc_config(Device* device, Bma530AccConfig* config) {
    uint8_t buffer[2] = {};
    error_t error = read_registers(device, REG_ACC_CONF_1, buffer, sizeof(buffer));
    if (error != ERROR_NONE) return error;

    config->odr = static_cast<Bma530Odr>(buffer[0] & 0x0F);
    config->bandwidth = static_cast<Bma530Bandwidth>((buffer[0] >> 4) & 0x07);
    config->power_mode = static_cast<Bma530PowerMode>(buffer[0] >> 7);
    config->range = static_cast<Bma530Range>(buffer[1] & 0x03);
    config->iir_roll_off = static_cast<Bma530IirRollOff>((buffer[1] >> 2) & 0x03);
    config->noise_mode = static_cast<Bma530NoiseMode>((buffer[1] >> 4) & 0x01);
    config->drdy_auto_clear = (buffer[1] & 0x80) != 0;
    return ERROR_NONE;
}

error_t bma530_set_acc_config(Device* device, const Bma530AccConfig* config) {
    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(internal);
    return write_acc_config(device, internal, *config);
}

error_t bma530_set_enabled(Device* device, bool enabled) {
    return write_register(device, REG_ACC_CONF_0, enabled ? SENSOR_CTRL_ENABLED : SENSOR_CTRL_DISABLED);
}

error_t bma530_set_suspended(Device* device, bool suspended) {
    return write_register(device, REG_CMD_SUSPEND, suspended ? 0x01 : 0x00);
}

error_t bma530_soft_reset(Device* device) {
    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(internal);
    return reset_to_defaults(device, internal);
}

error_t bma530_set_temperature_rate(Device* device, Bma530TemperatureRate rate) {
    if (rate > TEMPERATURE_RATE_MAX) return ERROR_INVALID_ARGUMENT;
    // Only the rate field is changed, the measurement source stays on the internal diode :D
    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(internal);

    uint8_t value = 0;
    error_t error = read_register(device, REG_TEMP_CONF, &value);
    if (error != ERROR_NONE) return error;
    value = static_cast<uint8_t>((value & ~0x07) | rate);
    return write_register(device, REG_TEMP_CONF, value);
}

error_t bma530_get_sensor_status(Device* device, Bma530SensorStatus* status) {
    uint8_t value = 0;
    error_t error = read_register(device, REG_SENSOR_STATUS, &value);
    if (error != ERROR_NONE) return error;

    status->acc_data_ready = (value & SENSOR_STATUS_ACC_DRDY) != 0;
    status->temperature_ready = (value & SENSOR_STATUS_TEMP_DRDY) != 0;
    status->sensor_ready = (value & SENSOR_STATUS_SENSOR_RDY) != 0;
    return ERROR_NONE;
}

error_t bma530_clear_sensor_status(Device* device, const Bma530SensorStatus* status) {
    uint8_t value = 0;
    if (status->acc_data_ready) value |= SENSOR_STATUS_ACC_DRDY;
    if (status->temperature_ready) value |= SENSOR_STATUS_TEMP_DRDY;
    return write_register(device, REG_SENSOR_STATUS, value);
}

error_t bma530_get_acc_offsets(Device* device, int16_t offsets[3]) {
    uint8_t buffer[6] = {};
    error_t error = read_registers(device, REG_ACC_OFFSET, buffer, sizeof(buffer));
    if (error != ERROR_NONE) return error;

    for (size_t axis = 0; axis < 3; axis++) {
        int16_t value = static_cast<int16_t>(buffer[axis * 2] | ((buffer[axis * 2 + 1] & 0x01) << 8));
        if ((value & OFFSET_SIGN_BIT) != 0) value = static_cast<int16_t>(value - OFFSET_RANGE);
        offsets[axis] = value;
    }
    return ERROR_NONE;
}

error_t bma530_set_acc_offsets(Device* device, const int16_t offsets[3]) {
    uint8_t buffer[6] = {};
    for (size_t axis = 0; axis < 3; axis++) {
        if (offsets[axis] < BMA530_OFFSET_MIN || offsets[axis] > BMA530_OFFSET_MAX) return ERROR_OUT_OF_RANGE;
        const auto encoded = static_cast<uint16_t>(offsets[axis]) & 0x1FF;
        buffer[axis * 2] = static_cast<uint8_t>(encoded & 0xFF);
        buffer[axis * 2 + 1] = static_cast<uint8_t>(encoded >> 8);
    }
    return write_registers(device, REG_ACC_OFFSET, buffer, sizeof(buffer));
}

error_t bma530_set_int_pin_config(Device* device, Bma530IntPin pin, const Bma530IntPinConfig* config) {
    if (pin > BMA530_INT_PIN_2 || config->mode > INT_MODE_MAX) return ERROR_INVALID_ARGUMENT;
    const uint8_t value = static_cast<uint8_t>(config->mode | (config->open_drain ? 0x04 : 0x00) | (config->active_high ? 0x08 : 0x00));
    return write_register(device, static_cast<uint8_t>(REG_INT_CONF_1 + pin), value);
}

error_t bma530_get_int_pin_config(Device* device, Bma530IntPin pin, Bma530IntPinConfig* config) {
    if (pin > BMA530_INT_PIN_2) return ERROR_INVALID_ARGUMENT;
    uint8_t value = 0;
    error_t error = read_register(device, static_cast<uint8_t>(REG_INT_CONF_1 + pin), &value);
    if (error != ERROR_NONE) return error;

    config->mode = static_cast<Bma530IntMode>(value & 0x03);
    config->open_drain = (value & 0x04) != 0;
    config->active_high = (value & 0x08) != 0;
    return ERROR_NONE;
}

error_t bma530_map_interrupt(Device* device, Bma530IntSource source, Bma530IntLine line) {
    if (static_cast<size_t>(source) >= sizeof(INTERRUPT_SOURCE_FIELDS) / sizeof(INTERRUPT_SOURCE_FIELDS[0])) return ERROR_INVALID_ARGUMENT;
    if (line > BMA530_INT_LINE_I3C) return ERROR_INVALID_ARGUMENT;

    const InterruptSourceField& field = INTERRUPT_SOURCE_FIELDS[source];
    const auto reg = static_cast<uint8_t>(REG_INT_MAP_0 + field.reg_offset);

    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(internal);

    uint8_t value = 0;
    error_t error = read_register(device, reg, &value);
    if (error != ERROR_NONE) return error;

    value = static_cast<uint8_t>((value & ~(INTERRUPT_FIELD_MASK << field.shift)) | (line << field.shift));
    return write_register(device, reg, value);
}

error_t bma530_get_int_status(Device* device, Bma530IntLine line, uint16_t* flags) {
    if (!is_status_line(line)) return ERROR_INVALID_ARGUMENT;

    uint8_t buffer[2] = {};
    error_t error = read_registers(device, interrupt_status_register(line), buffer, sizeof(buffer));
    if (error != ERROR_NONE) return error;

    *flags = static_cast<uint16_t>(buffer[0] | (buffer[1] << 8));
    return ERROR_NONE;
}

error_t bma530_clear_int_status(Device* device, Bma530IntLine line, uint16_t flags) {
    if (!is_status_line(line)) return ERROR_INVALID_ARGUMENT;

    const uint8_t buffer[2] = { static_cast<uint8_t>(flags & 0xFF), static_cast<uint8_t>(flags >> 8) };
    return write_registers(device, interrupt_status_register(line), buffer, sizeof(buffer));
}

error_t bma530_get_orientation_config(Device* device, Bma530OrientationConfig* config) {
    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(internal);

    error_t error = set_feature_window_address(device, ORIENTATION_BASE_ADDRESS);
    if (error != ERROR_NONE) return error;

    uint8_t buffer[FEATURE_DATA_PADDING + ORIENTATION_CONFIG_SIZE] = {};
    error = read_registers(device, REG_FEATURE_DATA_TX, buffer, sizeof(buffer));
    if (error != ERROR_NONE) return error;

    const uint8_t* data = buffer + FEATURE_DATA_PADDING;
    const auto settings = static_cast<uint16_t>(data[0] | (data[1] << 8));
    config->face_up_down_enable = (settings & 0x0001) != 0;
    config->mode = (settings >> 1) & 0x03;
    config->blocking = (settings >> 3) & 0x03;
    config->theta = (settings >> 5) & 0x3F;
    config->hold_time = (settings >> 11) & 0x1F;
    config->slope_threshold = data[2];
    config->hysteresis = data[3];
    return ERROR_NONE;
}

error_t bma530_set_orientation_config(Device* device, const Bma530OrientationConfig* config) {
    if (config->mode > ORIENTATION_MODE_MAX || config->blocking > ORIENTATION_BLOCKING_MAX ||
        config->theta > ORIENTATION_THETA_MAX || config->hold_time > ORIENTATION_HOLD_TIME_MAX) {
        return ERROR_INVALID_ARGUMENT;
    }

    const auto settings = static_cast<uint16_t>(
        (config->face_up_down_enable ? 0x0001 : 0x0000) |
        (config->mode << 1) |
        (config->blocking << 3) |
        (config->theta << 5) |
        (config->hold_time << 11)
    );
    const uint8_t buffer[ORIENTATION_CONFIG_SIZE] = {
        static_cast<uint8_t>(settings & 0xFF),
        static_cast<uint8_t>(settings >> 8),
        config->slope_threshold,
        config->hysteresis
    };

    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(internal);

    error_t error = set_feature_window_address(device, ORIENTATION_BASE_ADDRESS);
    if (error != ERROR_NONE) return error;
    return write_registers(device, REG_FEATURE_DATA_TX, buffer, sizeof(buffer));
}

error_t bma530_set_enabled_features(Device* device, uint8_t features) {
    error_t error = write_register(device, REG_GPR_0, features);
    if (error != ERROR_NONE) return error;
    return write_register(device, REG_GPR_CTRL, GPR_CTRL_UPDATE);
}

error_t bma530_get_feature_output(Device* device, Bma530FeatureOutput* output) {
    uint8_t buffer[FEATURE_OUTPUT_SIZE] = {};
    error_t error = read_registers(device, REG_GPR_1, buffer, sizeof(buffer));
    if (error != ERROR_NONE) return error;

    output->step_count = static_cast<uint32_t>(buffer[1]) | static_cast<uint32_t>(buffer[2]) << 8 | static_cast<uint32_t>(buffer[3]) << 16;
    output->orientation = static_cast<Bma530Orientation>(buffer[4] & 0x03);
    output->face_down = (buffer[4] & 0x04) != 0;
    output->activity = static_cast<Bma530Activity>((buffer[4] >> 3) & 0x03);
    output->generic_1_active = (buffer[4] & 0x20) != 0;
    output->generic_2_active = (buffer[4] & 0x40) != 0;
    output->generic_3_active = (buffer[4] & 0x80) != 0;
    return ERROR_NONE;
}

static ImuApi bma530_imu_api = {
    .read_accel = bma530_read_accel,
    .read_gyro = nullptr,
    .read_temperature = bma530_read_temperature
};

Driver bma530_driver = {
    .name = "bma530",
    .compatible = (const char*[]) { "bosch,bma530", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &bma530_imu_api,
    .device_type = &IMU_TYPE,
    .internal = nullptr
};

}
