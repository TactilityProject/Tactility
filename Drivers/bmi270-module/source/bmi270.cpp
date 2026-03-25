// SPDX-License-Identifier: Apache-2.0
#include <drivers/bmi270.h>
#include <bmi270_module.h>
#include <drivers/bmi270_config_data.h>
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#define TAG "BMI270"

static constexpr uint8_t REG_CHIP_ID = 0x00; // read: expect 0x24
static constexpr uint8_t REG_DATA_ACC = 0x0C; // 6 bytes: acc X/Y/Z LSB/MSB
static constexpr uint8_t REG_DATA_GYR = 0x12; // 6 bytes: gyr X/Y/Z LSB/MSB
static constexpr uint8_t REG_INTERNAL_ST = 0x21; // bit0: init done
static constexpr uint8_t REG_ACC_CONF = 0x40; // ODR + BWP + filter_perf
static constexpr uint8_t REG_ACC_RANGE = 0x41; // range selector
static constexpr uint8_t REG_GYR_CONF = 0x42; // ODR + BWP
static constexpr uint8_t REG_GYR_RANGE = 0x43; // range selector
static constexpr uint8_t REG_INIT_CTRL = 0x59; // 0=start upload, 1=done
static constexpr uint8_t REG_INIT_ADDR_0 = 0x5B; // burst address low nibble
// REG_INIT_ADDR_1 = 0x5C written together with 0x5B (2-byte burst)
static constexpr uint8_t REG_INIT_DATA = 0x5E; // config burst write target
static constexpr uint8_t REG_PWR_CONF = 0x7C; // 0=disable adv. power save
static constexpr uint8_t REG_PWR_CTRL = 0x7D; // bit1=gyr_en, bit2=acc_en
static constexpr uint8_t REG_CMD = 0x7E; // 0xB6 = soft reset

// ACC_CONF: filter_perf=1, bwp=normal(2), odr=100Hz(8) → 0xA8
static constexpr uint8_t ACC_CONF_VAL = 0xA8;
static constexpr uint8_t ACC_RANGE_VAL = 0x02; // ±8g
// GYR_CONF: filter_perf=1, noise_perf=1, bwp=normal(2), odr=100Hz(8) → 0xE8
static constexpr uint8_t GYR_CONF_VAL = 0xE8;
static constexpr uint8_t GYR_RANGE_VAL = 0x00; // ±2000°/s

// Scaling: full-scale / 2^15
static constexpr float ACCEL_SCALE = 8.0f / 32768.0f; // g per LSB (±8g)
static constexpr float GYRO_SCALE = 2000.0f / 32768.0f; // °/s per LSB (±2000°/s)

// Config upload chunk size (bytes of config data per I2C transaction)
static constexpr size_t CHUNK_SIZE = 64;

static constexpr TickType_t I2C_TIMEOUT_TICKS = pdMS_TO_TICKS(10);

#define GET_CONFIG(device) (static_cast<const Bmi270Config*>((device)->config))

// region Helpers

static bool configure(Device* i2c_controller, uint8_t address) {
    // Disable advanced power save before uploading
    if (i2c_controller_register8_set(i2c_controller, address, REG_PWR_CONF, 0x00, I2C_TIMEOUT_TICKS) != ERROR_NONE) return false;
    vTaskDelay(pdMS_TO_TICKS(1));

    // Signal start of config upload
    if (i2c_controller_register8_set(i2c_controller, address, REG_INIT_CTRL, 0x00, I2C_TIMEOUT_TICKS) != ERROR_NONE) return false;

    // Upload config in CHUNK_SIZE-byte bursts
    // The half-word address (hwAddr) increments by CHUNK_SIZE/2 per chunk
    constexpr size_t config_data_size = sizeof(bmi270_config_data);
    for (size_t offset = 0; offset < config_data_size; offset += CHUNK_SIZE) {
        // Set INIT_ADDR_0 and INIT_ADDR_1 (consecutive registers 0x5B/0x5C)
        auto hardware_address = static_cast<uint16_t>(offset / 2);
        uint8_t address_buffer[2] = {
            static_cast<uint8_t>(hardware_address & 0x0Fu),         // INIT_ADDR_0: bits [3:0]
            static_cast<uint8_t>((hardware_address >> 4) & 0xFFu)   // INIT_ADDR_1: bits [11:4]
        };

        if (i2c_controller_write_register(i2c_controller, address, REG_INIT_ADDR_0, address_buffer, 2, I2C_TIMEOUT_TICKS) != ERROR_NONE) return false;

        // Write chunk to INIT_DATA register.
        // Copy to a stack buffer first: the config array may live in DROM (SPI flash)
        // which is not DMA-accessible; the I2C driver requires DRAM-backed buffers.
        size_t chunk_length = (offset + CHUNK_SIZE <= config_data_size) ? CHUNK_SIZE : (config_data_size - offset);
        uint8_t chunk_buffer[CHUNK_SIZE];
        memcpy(chunk_buffer, bmi270_config_data + offset, chunk_length);
        if (i2c_controller_write_register(i2c_controller, address, REG_INIT_DATA, chunk_buffer, static_cast<uint16_t>(chunk_length), I2C_TIMEOUT_TICKS) != ERROR_NONE) return false;
    }

    // Signal end of config upload
    if (i2c_controller_register8_set(i2c_controller, address, REG_INIT_CTRL, 0x01, I2C_TIMEOUT_TICKS) != ERROR_NONE) return false;
    vTaskDelay(pdMS_TO_TICKS(20));

    // Verify initialization
    uint8_t status = 0;
    if (i2c_controller_register8_get(i2c_controller, address, REG_INTERNAL_ST, &status, I2C_TIMEOUT_TICKS) != ERROR_NONE) return false;
    return (status & 0x01u) != 0; // bit 0 = init_ok
}

// endregion

// region Driver lifecycle

static error_t start(Device* device) {
    auto* i2c_controller = device_get_parent(device);
    if (device_get_type(i2c_controller) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto address = GET_CONFIG(device)->address;

    // Verify chip ID
    uint8_t chip_id= 0;
    if (i2c_controller_register8_get(i2c_controller, address, REG_CHIP_ID, &chip_id, I2C_TIMEOUT_TICKS) != ERROR_NONE || chip_id != 0x24) {
        return ERROR_RESOURCE;
    }

    // Soft reset — clears all registers; datasheet specifies 2 ms startup time.
    // Use 20 ms to be safe and allow the chip to fully re-initialise before
    // any further I2C traffic (a second chip-ID read immediately after reset
    // is unreliable and not part of the Bosch SensorAPI init flow).
    if (i2c_controller_register8_set(i2c_controller, address, REG_CMD, 0xB6, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    vTaskDelay(pdMS_TO_TICKS(20));

    // Upload 8KB configuration (enables internal feature engine)
    if (!configure(i2c_controller, address)) {
        return ERROR_RESOURCE;
    }

    // Configure accelerometer: ODR=100Hz, normal filter, ±8g
    if (i2c_controller_register8_set(i2c_controller, address, REG_ACC_CONF,  ACC_CONF_VAL, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    if (i2c_controller_register8_set(i2c_controller, address, REG_ACC_RANGE, ACC_RANGE_VAL, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;

    // Configure gyroscope: ODR=100Hz, normal filter, ±2000°/s
    if (i2c_controller_register8_set(i2c_controller, address, REG_GYR_CONF,  GYR_CONF_VAL, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    if (i2c_controller_register8_set(i2c_controller, address, REG_GYR_RANGE, GYR_RANGE_VAL, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;

    // Enable accelerometer and gyroscope (bit1=gyr_en, bit2=acc_en)
    if (i2c_controller_register8_set(i2c_controller, address, REG_PWR_CTRL, 0x06, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    vTaskDelay(pdMS_TO_TICKS(2));

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* i2c_controller = device_get_parent(device);
    if (device_get_type(i2c_controller) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto address = GET_CONFIG(device)->address;

    // Disable accelerometer and gyroscope (clear bit1=gyr_en, bit2=acc_en)
    if (i2c_controller_register8_set(i2c_controller, address, REG_PWR_CTRL, 0x00, I2C_TIMEOUT_TICKS) != ERROR_NONE) {
        LOG_E(TAG, "Failed to put BMI270 to sleep");
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

// endregion

extern "C" {

error_t bmi270_read(Device* device, Bmi270Data* data) {
    auto* i2c_controller = device_get_parent(device);

    auto address = GET_CONFIG(device)->address;

    // Burst-read 12 bytes: acc X/Y/Z (6 bytes) + gyro X/Y/Z (6 bytes)
    // Registers: 0x0C–0x17 are contiguous for acc+gyro in I2C mode (no dummy byte)
    uint8_t buffer[12] = {};
    error_t error = i2c_controller_read_register(i2c_controller, address, REG_DATA_ACC, buffer, sizeof(buffer), I2C_TIMEOUT_TICKS);
    if (error != ERROR_NONE) return error;

    auto toI16 = [](uint8_t lo, uint8_t hi) -> int16_t {
        return static_cast<int16_t>(static_cast<uint16_t>(hi) << 8 | lo);
    };

    data->ax = toI16(buffer[0],  buffer[1])  * ACCEL_SCALE;
    data->ay = toI16(buffer[2],  buffer[3])  * ACCEL_SCALE;
    data->az = toI16(buffer[4],  buffer[5])  * ACCEL_SCALE;
    data->gx = toI16(buffer[6],  buffer[7])  * GYRO_SCALE;
    data->gy = toI16(buffer[8],  buffer[9])  * GYRO_SCALE;
    data->gz = toI16(buffer[10], buffer[11]) * GYRO_SCALE;

    return ERROR_NONE;
}

Driver bmi270_driver = {
    .name = "bmi270",
    .compatible = (const char*[]) { "bosch,bmi270", nullptr},
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &bmi270_module,
    .internal = nullptr
};

}