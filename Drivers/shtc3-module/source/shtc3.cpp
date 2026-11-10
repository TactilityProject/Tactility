// SPDX-License-Identifier: Apache-2.0
#include <drivers/shtc3.h>
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "SHTC3"

static constexpr uint16_t CMD_WAKEUP = 0x3517;
static constexpr uint16_t CMD_SLEEP = 0xB098;
static constexpr uint16_t CMD_READ_ID = 0xEFC8;
// Normal mode, temperature first, without clock stretching
static constexpr uint16_t CMD_MEASURE = 0x7866;

static constexpr uint16_t ID_MASK = 0x083F;
static constexpr uint16_t ID_VALUE = 0x0807;

static constexpr TickType_t I2C_TIMEOUT_TICKS = pdMS_TO_TICKS(50);
// Wake up takes at most 240 us and a normal mode measurement at most 12.1 ms
static constexpr TickType_t WAKEUP_TICKS = pdMS_TO_TICKS(1);
static constexpr TickType_t MEASURE_TICKS = pdMS_TO_TICKS(15);

#define GET_CONFIG(device) (static_cast<const Shtc3Config*>((device)->config))

static error_t send_command(Device* device, uint16_t command) {
    const uint8_t data[2] = { static_cast<uint8_t>(command >> 8), static_cast<uint8_t>(command & 0xFF) };
    return i2c_controller_write(device_get_parent(device), GET_CONFIG(device)->address, data, sizeof(data), I2C_TIMEOUT_TICKS);
}

static uint8_t crc8(const uint8_t* data, size_t length) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

// Reads words that each have a checksum byte
static error_t read_words(Device* device, uint16_t* words, size_t count) {
    uint8_t data[6] = {};
    const size_t size = count * 3;
    error_t error = i2c_controller_read(device_get_parent(device), GET_CONFIG(device)->address, data, size, I2C_TIMEOUT_TICKS);
    if (error != ERROR_NONE) return error;
    for (size_t i = 0; i < count; i++) {
        if (crc8(&data[i * 3], 2) != data[i * 3 + 2]) {
            return ERROR_INVALID_STATE;
        }
        words[i] = static_cast<uint16_t>((data[i * 3] << 8) | data[i * 3 + 1]);
    }
    return ERROR_NONE;
}

static error_t wake(Device* device) {
    error_t error = send_command(device, CMD_WAKEUP);
    if (error == ERROR_NONE) {
        vTaskDelay(WAKEUP_TICKS);
    }
    return error;
}

static error_t start(Device* device) {
    auto* i2c_controller = device_get_parent(device);
    if (device_get_type(i2c_controller) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    error_t error = wake(device);
    uint16_t id = 0;
    if (error == ERROR_NONE) error = send_command(device, CMD_READ_ID);
    if (error == ERROR_NONE) error = read_words(device, &id, 1);
    send_command(device, CMD_SLEEP);

    if (error != ERROR_NONE) {
        LOG_E(TAG, "No sensor at 0x%02X", GET_CONFIG(device)->address);
        return ERROR_RESOURCE;
    }
    if ((id & ID_MASK) != ID_VALUE) {
        LOG_E(TAG, "Unexpected ID 0x%04X", id);
        return ERROR_NOT_SUPPORTED;
    }
    return ERROR_NONE;
}

static error_t stop(Device*) {
    return ERROR_NONE;
}

extern "C" {

error_t shtc3_read(Device* device, float* celsius, float* humidity_percent) {
    device_lock(device);
    error_t error = wake(device);
    if (error == ERROR_NONE) error = send_command(device, CMD_MEASURE);
    uint16_t words[2] = {};
    if (error == ERROR_NONE) {
        vTaskDelay(MEASURE_TICKS);
        error = read_words(device, words, 2);
    }
    send_command(device, CMD_SLEEP);
    device_unlock(device);

    if (error != ERROR_NONE) return error;
    *celsius = -45.0f + 175.0f * static_cast<float>(words[0]) / 65536.0f;
    *humidity_percent = 100.0f * static_cast<float>(words[1]) / 65536.0f;
    return ERROR_NONE;
}

Driver shtc3_driver = {
    .name = "shtc3",
    .compatible = (const char*[]) { "sensirion,shtc3", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = nullptr,
    .device_type = nullptr,
    .internal = nullptr
};

} // extern "C"
