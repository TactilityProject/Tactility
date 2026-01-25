// SPDX-License-Identifier: Apache-2.0
#include <driver/i2c.h>

#include <Tactility/Driver.h>
#include <Tactility/drivers/Esp32I2c.h>

#include "Tactility/ErrorEsp32.h"

#include <Tactility/Log.h>
#include <Tactility/drivers/I2cController.h>

#define TAG LOG_TAG(esp32_i2c)

struct InternalData {
    Mutex mutex { 0 };

    InternalData() {
        mutex_construct(&mutex);
    }

    ~InternalData() {
        mutex_destruct(&mutex);
    }
};

#define GET_CONFIG(device) ((Esp32I2cConfig*)device->config)
#define GET_DATA(device) ((InternalData*)device->internal.driver_data)

#define lock(data) mutex_lock(&data->mutex);
#define unlock(data) mutex_unlock(&data->mutex);

extern "C" {

static int read(Device* device, uint8_t address, uint8_t* data, size_t data_size, TickType_t timeout) {
    vPortAssertIfInISR();
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t esp_error = i2c_master_read_from_device(GET_CONFIG(device)->port, address, data, data_size, timeout);
    unlock(driver_data);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_error);
    return esp_err_to_error(esp_error);
}

static int write(Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    vPortAssertIfInISR();
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t esp_error = i2c_master_write_to_device(GET_CONFIG(device)->port, address, data, dataSize, timeout);
    unlock(driver_data);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_error);
    return esp_err_to_error(esp_error);
}

static int write_read(Device* device, uint8_t address, const uint8_t* write_data, size_t write_data_size, uint8_t* read_data, size_t read_data_size, TickType_t timeout) {
    vPortAssertIfInISR();
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t esp_error = i2c_master_write_read_device(GET_CONFIG(device)->port, address, write_data, write_data_size, read_data, read_data_size, timeout);
    unlock(driver_data);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_error);
    return esp_err_to_error(esp_error);
}

static int start(Device* device) {
    ESP_LOGI(TAG, "start %s", device->name);
    auto* data = new InternalData();
    device_set_driver_data(device, data);
    return ERROR_NONE;
}

static int stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    auto* driver_data = static_cast<InternalData*>(device_get_driver_data(device));
    device_set_driver_data(device, nullptr);
    delete driver_data;
    return ERROR_NONE;
}

const static I2cControllerApi esp32_i2c_api = {
    .read = read,
    .write = write,
    .write_read = write_read
};

Driver esp32_i2c_driver = {
    .name = "esp32_i2c",
    .compatible = (const char*[]) { "espressif,esp32-i2c", nullptr },
    .startDevice = start,
    .stopDevice = stop,
    .api = (void*)&esp32_i2c_api,
    .deviceType = &I2C_CONTROLLER_TYPE,
    .internal = { 0 }
};

} // extern "C"
