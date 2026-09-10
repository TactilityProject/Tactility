// SPDX-License-Identifier: Apache-2.0
#include <drivers/qmi8658.h>
#include <qmi8658_module.h>
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#define TAG "QMI8658"

// Register map (QMI8658 datasheet)
static constexpr uint8_t REG_WHO_AM_I = 0x00; // chip ID — expect 0x05
static constexpr uint8_t REG_CTRL1    = 0x02; // serial interface config
static constexpr uint8_t REG_CTRL2    = 0x03; // accel: range[6:4] + ODR[3:0]
static constexpr uint8_t REG_CTRL3    = 0x04; // gyro:  range[6:4] + ODR[3:0]
static constexpr uint8_t REG_CTRL7    = 0x08; // sensor enable: bit0=accel, bit1=gyro
static constexpr uint8_t REG_TEMP_L   = 0x33; // 2 bytes: on-die temperature
static constexpr uint8_t REG_AX_L     = 0x35; // first data register (accel X LSB)
static constexpr uint8_t REG_GX_L     = 0x3B; // first gyro data register (gyro X LSB)

static constexpr uint8_t WHO_AM_I_VALUE = 0x05;

// CTRL1: auto address increment (bit 6), little-endian (bit 5 = 0)
static constexpr uint8_t CTRL1_VAL = 0x40;
// CTRL2: accel ±8G (aFS=0x02 → bits[6:4]=010) + 1000Hz ODR (0x03 → bits[3:0]=0011) = 0x23
static constexpr uint8_t CTRL2_VAL = 0x23;
// CTRL3: gyro ±2048DPS (gFS=0x06 → bits[6:4]=110) + 1000Hz ODR (0x03 → bits[3:0]=0011) = 0x63
static constexpr uint8_t CTRL3_VAL = 0x63;
// CTRL7: enable accel (bit0) + gyro (bit1)
static constexpr uint8_t CTRL7_ENABLE = 0x03;

// Scaling: full-scale / 2^15
static constexpr float ACCEL_SCALE = 8.0f / 32768.0f;    // g per LSB (±8g) → 1/4096
static constexpr float GYRO_SCALE  = 2048.0f / 32768.0f; // °/s per LSB (±2048°/s) → 1/16

static constexpr TickType_t I2C_TIMEOUT_TICKS = pdMS_TO_TICKS(10);

#define GET_CONFIG(device) (static_cast<const Qmi8658Config*>((device)->config))

// region Driver lifecycle

static error_t start(Device* device) {
    auto* i2c_controller = device_get_parent(device);
    if (device_get_type(i2c_controller) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto address = GET_CONFIG(device)->address;

    // Verify chip ID
    uint8_t who_am_i = 0;
    if (i2c_controller_register8_get(i2c_controller, address, REG_WHO_AM_I, &who_am_i, I2C_TIMEOUT_TICKS) != ERROR_NONE
        || who_am_i != WHO_AM_I_VALUE) {
        LOG_E(TAG, "WHO_AM_I mismatch: got 0x%02X, expected 0x%02X", who_am_i, WHO_AM_I_VALUE);
        return ERROR_RESOURCE;
    }

    // Disable all sensors during configuration
    if (i2c_controller_register8_set(i2c_controller, address, REG_CTRL7, 0x00, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;

    // Serial interface: auto address increment, little-endian
    if (i2c_controller_register8_set(i2c_controller, address, REG_CTRL1, CTRL1_VAL, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;

    // Accel: ±8g, 1000Hz ODR
    if (i2c_controller_register8_set(i2c_controller, address, REG_CTRL2, CTRL2_VAL, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;

    // Gyro: ±2048°/s, 1000Hz ODR
    if (i2c_controller_register8_set(i2c_controller, address, REG_CTRL3, CTRL3_VAL, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;

    // Enable accel + gyro
    if (i2c_controller_register8_set(i2c_controller, address, REG_CTRL7, CTRL7_ENABLE, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* i2c_controller = device_get_parent(device);
    if (device_get_type(i2c_controller) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto address = GET_CONFIG(device)->address;

    // Put device to sleep
    if (i2c_controller_register8_set(i2c_controller, address, REG_CTRL7, 0x00, I2C_TIMEOUT_TICKS) != ERROR_NONE) {
        LOG_E(TAG, "Failed to put QMI8658 to sleep");
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

// endregion

extern "C" {

error_t qmi8658_read(Device* device, ImuData* data) {
    auto* i2c_controller = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;

    // Burst-read 12 bytes starting at AX_L (0x35): accel X/Y/Z then gyro X/Y/Z
    // QMI8658 is little-endian (LSB first), same as BMI270
    uint8_t buf[12] = {};
    error_t error = i2c_controller_read_register(i2c_controller, address, REG_AX_L, buf, sizeof(buf), I2C_TIMEOUT_TICKS);
    if (error != ERROR_NONE) return error;

    auto toI16 = [](uint8_t lo, uint8_t hi) -> int16_t {
        return static_cast<int16_t>(static_cast<uint16_t>(hi) << 8 | lo);
    };

    data->ax = toI16(buf[0],  buf[1])  * ACCEL_SCALE;
    data->ay = toI16(buf[2],  buf[3])  * ACCEL_SCALE;
    data->az = toI16(buf[4],  buf[5])  * ACCEL_SCALE;
    data->gx = toI16(buf[6],  buf[7])  * GYRO_SCALE;
    data->gy = toI16(buf[8],  buf[9])  * GYRO_SCALE;
    data->gz = toI16(buf[10], buf[11]) * GYRO_SCALE;

    return ERROR_NONE;
}

error_t qmi8658_read_accel(Device* device, ImuAccelData* data) {
    auto* i2c_controller = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;

    uint8_t buf[6] = {};
    error_t error = i2c_controller_read_register(i2c_controller, address, REG_AX_L, buf, sizeof(buf), I2C_TIMEOUT_TICKS);
    if (error != ERROR_NONE) return error;

    auto toI16 = [](uint8_t lo, uint8_t hi) -> int16_t {
        return static_cast<int16_t>(static_cast<uint16_t>(hi) << 8 | lo);
    };

    data->ax = toI16(buf[0], buf[1]) * ACCEL_SCALE;
    data->ay = toI16(buf[2], buf[3]) * ACCEL_SCALE;
    data->az = toI16(buf[4], buf[5]) * ACCEL_SCALE;

    return ERROR_NONE;
}

error_t qmi8658_read_gyro(Device* device, ImuGyroData* data) {
    auto* i2c_controller = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;

    uint8_t buf[6] = {};
    error_t error = i2c_controller_read_register(i2c_controller, address, REG_GX_L, buf, sizeof(buf), I2C_TIMEOUT_TICKS);
    if (error != ERROR_NONE) return error;

    auto toI16 = [](uint8_t lo, uint8_t hi) -> int16_t {
        return static_cast<int16_t>(static_cast<uint16_t>(hi) << 8 | lo);
    };

    data->gx = toI16(buf[0], buf[1]) * GYRO_SCALE;
    data->gy = toI16(buf[2], buf[3]) * GYRO_SCALE;
    data->gz = toI16(buf[4], buf[5]) * GYRO_SCALE;

    return ERROR_NONE;
}

error_t qmi8658_read_temperature(Device* device, float* temperature_c) {
    auto* i2c_controller = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;

    uint8_t buf[2] = {};
    error_t error = i2c_controller_read_register(i2c_controller, address, REG_TEMP_L, buf, sizeof(buf), I2C_TIMEOUT_TICKS);
    if (error != ERROR_NONE) return error;

    auto raw = static_cast<int16_t>(static_cast<uint16_t>(buf[1]) << 8 | buf[0]);

    // Datasheet: temperature = raw / 256 (°C)
    *temperature_c = static_cast<float>(raw) / 256.0f;
    return ERROR_NONE;
}

ImuApi qmi8658_imu_api = {
    .read = qmi8658_read,
    .read_accel = qmi8658_read_accel,
    .read_gyro = qmi8658_read_gyro,
    .read_temperature = qmi8658_read_temperature
};

Driver qmi8658_driver = {
    .name = "qmi8658",
    .compatible = (const char*[]) { "qst,qmi8658", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &qmi8658_imu_api,
    .device_type = &IMU_TYPE,
    .owner = &qmi8658_module,
    .internal = nullptr
};

} // extern "C"
