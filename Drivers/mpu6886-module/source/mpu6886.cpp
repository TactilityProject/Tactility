// SPDX-License-Identifier: Apache-2.0
#include <drivers/mpu6886.h>
#include <mpu6886_module.h>
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#define TAG "MPU6886"

// Register map
static constexpr uint8_t REG_SMPLRT_DIV    = 0x19; // sample rate divider
static constexpr uint8_t REG_CONFIG        = 0x1A; // DLPF config
static constexpr uint8_t REG_GYRO_CONFIG   = 0x1B; // gyro full-scale select
static constexpr uint8_t REG_ACCEL_CONFIG  = 0x1C; // accel full-scale select
static constexpr uint8_t REG_ACCEL_CONFIG2 = 0x1D; // accel low-pass filter
static constexpr uint8_t REG_INT_PIN_CFG   = 0x37; // interrupt pin config
static constexpr uint8_t REG_INT_ENABLE    = 0x38; // interrupt enable
static constexpr uint8_t REG_ACCEL_XOUT_H  = 0x3B; // first accel output register
static constexpr uint8_t REG_GYRO_XOUT_H   = 0x43; // first gyro output register
static constexpr uint8_t REG_USER_CTRL     = 0x6A; // user control (DMP, FIFO, I2C)
static constexpr uint8_t REG_PWR_MGMT_1    = 0x6B; // power management 1
static constexpr uint8_t REG_PWR_MGMT_2    = 0x6C; // power management 2
static constexpr uint8_t REG_FIFO_EN       = 0x23; // FIFO enable
static constexpr uint8_t REG_WHO_AM_I      = 0x75; // chip ID — expect 0x19

static constexpr uint8_t WHO_AM_I_VALUE    = 0x19;

// Configuration values
// GYRO_CONFIG: FS_SEL=3 (±2000°/s), FCHOICE_B=00 → 0x18
static constexpr uint8_t GYRO_CONFIG_VAL   = 0x18;
// ACCEL_CONFIG: AFS_SEL=2 (±8g) → 0x10
static constexpr uint8_t ACCEL_CONFIG_VAL  = 0x10;
// CONFIG: DLPF_CFG=1 → gyro BW=176Hz, temp BW=188Hz → 0x01
static constexpr uint8_t CONFIG_VAL        = 0x01;
// SMPLRT_DIV: sample rate = 1kHz / (1 + 5) = 166Hz → 0x05
static constexpr uint8_t SMPLRT_DIV_VAL   = 0x05;

// Scaling: full-scale / 2^15
static constexpr float ACCEL_SCALE = 8.0f / 32768.0f;    // g per LSB (±8g)
static constexpr float GYRO_SCALE  = 2000.0f / 32768.0f; // °/s per LSB (±2000°/s)

static constexpr TickType_t I2C_TIMEOUT_TICKS = pdMS_TO_TICKS(10);

#define GET_CONFIG(device) (static_cast<const Mpu6886Config*>((device)->config))

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

    // Wake from sleep (clear all PWR_MGMT_1 bits)
    if (i2c_controller_register8_set(i2c_controller, address, REG_PWR_MGMT_1, 0x00, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    vTaskDelay(pdMS_TO_TICKS(10));

    // Device reset (bit 7)
    if (i2c_controller_register8_set(i2c_controller, address, REG_PWR_MGMT_1, 0x80, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    vTaskDelay(pdMS_TO_TICKS(10));

    // Select auto clock (CLKSEL=1: PLL with gyro reference when available)
    if (i2c_controller_register8_set(i2c_controller, address, REG_PWR_MGMT_1, 0x01, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    vTaskDelay(pdMS_TO_TICKS(10));

    // Configure accel: ±8g
    if (i2c_controller_register8_set(i2c_controller, address, REG_ACCEL_CONFIG,  ACCEL_CONFIG_VAL, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    // Configure gyro: ±2000°/s
    if (i2c_controller_register8_set(i2c_controller, address, REG_GYRO_CONFIG,   GYRO_CONFIG_VAL,  I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    // DLPF: gyro BW=176Hz
    if (i2c_controller_register8_set(i2c_controller, address, REG_CONFIG,        CONFIG_VAL,       I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    // Sample rate: 1kHz / (1+5) = 166Hz
    if (i2c_controller_register8_set(i2c_controller, address, REG_SMPLRT_DIV,    SMPLRT_DIV_VAL,   I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    // Clear interrupt enables before reconfiguring
    if (i2c_controller_register8_set(i2c_controller, address, REG_INT_ENABLE,    0x00,             I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    // Accel low-pass filter: ACCEL_FCHOICE_B=0, A_DLPF_CFG=0
    if (i2c_controller_register8_set(i2c_controller, address, REG_ACCEL_CONFIG2, 0x00,             I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    // Disable DMP and FIFO
    if (i2c_controller_register8_set(i2c_controller, address, REG_USER_CTRL,     0x00,             I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    if (i2c_controller_register8_set(i2c_controller, address, REG_FIFO_EN,       0x00,             I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    // Interrupt: active-high (ACTL=0), push-pull (OPEN=0), latched (LATCH_INT_EN=1), cleared on any read (INT_ANYRD_2CLEAR=1) → 0x30
    if (i2c_controller_register8_set(i2c_controller, address, REG_INT_PIN_CFG,   0x30,             I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    // Enable DATA_RDY interrupt
    if (i2c_controller_register8_set(i2c_controller, address, REG_INT_ENABLE,    0x01,             I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* i2c_controller = device_get_parent(device);
    if (device_get_type(i2c_controller) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto address = GET_CONFIG(device)->address;

    // Put device to sleep (set SLEEP bit in PWR_MGMT_1)
    if (i2c_controller_register8_set(i2c_controller, address, REG_PWR_MGMT_1, 0x40, I2C_TIMEOUT_TICKS) != ERROR_NONE) {
        LOG_E(TAG, "Failed to put MPU6886 to sleep");
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

// endregion

extern "C" {

error_t mpu6886_read(Device* device, Mpu6886Data* data) {
    auto* i2c_controller = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;

    // MPU6886 is big-endian (MSB first), unlike BMI270
    auto toI16 = [](uint8_t hi, uint8_t lo) -> int16_t {
        return static_cast<int16_t>(static_cast<uint16_t>(hi) << 8 | lo);
    };

    // Burst read: accel (6) + temp (2) + gyro (6) = 14 bytes at 0x3B
    uint8_t buf[14] = {};
    error_t error = i2c_controller_read_register(i2c_controller, address, REG_ACCEL_XOUT_H, buf, sizeof(buf), I2C_TIMEOUT_TICKS);
    if (error != ERROR_NONE) return error;

    data->ax = toI16(buf[0], buf[1]) * ACCEL_SCALE;
    data->ay = toI16(buf[2], buf[3]) * ACCEL_SCALE;
    data->az = toI16(buf[4], buf[5]) * ACCEL_SCALE;
    // buf[6..7] = temperature (skipped)
    data->gx = toI16(buf[8], buf[9]) * GYRO_SCALE;
    data->gy = toI16(buf[10], buf[11]) * GYRO_SCALE;
    data->gz = toI16(buf[12], buf[13]) * GYRO_SCALE;

    return ERROR_NONE;
}

Driver mpu6886_driver = {
    .name = "mpu6886",
    .compatible = (const char*[]) { "invensense,mpu6886", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &mpu6886_module,
    .internal = nullptr
};

} // extern "C"
