// SPDX-License-Identifier: Apache-2.0
#include <driver/i2c.h>

#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#include <tactility/error_esp32.h>
#include <tactility/drivers/esp32_i2c.h>

#define TAG LOG_TAG(esp32_i2c)
#define ACK_CHECK_EN 1

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

static error_t read(Device* device, uint8_t address, uint8_t* data, size_t data_size, TickType_t timeout) {
    vPortAssertIfInISR();
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t esp_error = i2c_master_read_from_device(GET_CONFIG(device)->port, address, data, data_size, timeout);
    unlock(driver_data);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_error);
    return esp_err_to_error(esp_error);
}

static error_t write(Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    vPortAssertIfInISR();
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t esp_error = i2c_master_write_to_device(GET_CONFIG(device)->port, address, data, dataSize, timeout);
    unlock(driver_data);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_error);
    return esp_err_to_error(esp_error);
}

static error_t write_read(Device* device, uint8_t address, const uint8_t* write_data, size_t write_data_size, uint8_t* read_data, size_t read_data_size, TickType_t timeout) {
    vPortAssertIfInISR();
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t esp_error = i2c_master_write_read_device(GET_CONFIG(device)->port, address, write_data, write_data_size, read_data, read_data_size, timeout);
    unlock(driver_data);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_error);
    return esp_err_to_error(esp_error);
}

static error_t read_register(Device* device, uint8_t address, uint8_t reg, uint8_t* data, size_t dataSize, TickType_t timeout) {
    auto* driver_data = GET_DATA(device);

    lock(driver_data);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    // Set address pointer
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write(cmd, &reg, 1, ACK_CHECK_EN);
    // Read length of response from current pointer
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_READ, ACK_CHECK_EN);
    if (dataSize > 1) {
        i2c_master_read(cmd, data, dataSize - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + dataSize - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    // TODO: We're passing an inaccurate timeout value as we already lost time with locking
    esp_err_t esp_error = i2c_master_cmd_begin(GET_CONFIG(device)->port, cmd, timeout);
    i2c_cmd_link_delete(cmd);
    unlock(driver_data);

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_error);
    return esp_err_to_error(esp_error);
}

static error_t write_register(Device* device, uint8_t address, uint8_t reg, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    auto* driver_data = GET_DATA(device);

    lock(driver_data);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK_CHECK_EN);
    i2c_master_write(cmd, (uint8_t*) data, dataSize, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    // TODO: We're passing an inaccurate timeout value as we already lost time with locking
    esp_err_t esp_error = i2c_master_cmd_begin(GET_CONFIG(device)->port, cmd, timeout);
    i2c_cmd_link_delete(cmd);
    unlock(driver_data);

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_error);
    return esp_err_to_error(esp_error);
}

static error_t start(Device* device) {
    ESP_LOGI(TAG, "start %s", device->name);
    auto* data = new InternalData();
    device_set_driver_data(device, data);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    auto* driver_data = static_cast<InternalData*>(device_get_driver_data(device));
    device_set_driver_data(device, nullptr);
    delete driver_data;
    return ERROR_NONE;
}


const static I2cControllerApi esp32_i2c_api = {
    .read = read,
    .write = write,
    .write_read = write_read,
    .read_register = read_register,
    .write_register = write_register
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
