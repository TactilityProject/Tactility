#include <driver/i2c.h>

#include <Tactility/Driver.h>
#include <Tactility/drivers/Esp32I2c.h>
#include <Tactility/drivers/I2cController.h>
#include <Tactility/Log.h>

#define TAG LOG_TAG(esp32_i2c)

struct InternalData {
    Mutex mutex {};

    InternalData() {
        mutex_construct(&mutex);
    }

    ~InternalData() {
        mutex_destruct(&mutex);
    }
};

#define GET_CONFIG(device) ((Esp32I2cConfig*)device->internal.driver_data)
#define GET_DATA(device) ((InternalData*)device->internal.driver_data)

#define lock(data) mutex_lock(&data->mutex);
#define unlock(data) mutex_unlock(&data->mutex);

extern "C" {

static bool read(Device* device, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout) {
    vPortAssertIfInISR();
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t result = i2c_master_read_from_device(GET_CONFIG(device)->port, address, data, dataSize, timeout);
    unlock(driver_data);
    ESP_ERROR_CHECK_WITHOUT_ABORT(result);
    return result == ESP_OK;
}

static bool write(Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    vPortAssertIfInISR();
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t result = i2c_master_write_to_device(GET_CONFIG(device)->port, address, data, dataSize, timeout);
    unlock(driver_data);
    ESP_ERROR_CHECK_WITHOUT_ABORT(result);
    return result == ESP_OK;
}

static bool write_read(Device* device, uint8_t address, const uint8_t* write_data, size_t write_data_size, uint8_t* read_data, size_t read_data_size, TickType_t timeout) {
    vPortAssertIfInISR();
    auto* driver_data = GET_DATA(device);
    lock(driver_data);
    const esp_err_t result = i2c_master_write_read_device(GET_CONFIG(device)->port, address, write_data, write_data_size, read_data, read_data_size, timeout);
    unlock(driver_data)
    ESP_ERROR_CHECK_WITHOUT_ABORT(result);
    return result == ESP_OK;
}

static int start(Device* device) {
    ESP_LOGI(TAG, "start %s", device->name);
    auto* data = new InternalData();
    device_set_driver_data(device, data);
    return 0;
}

static int stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    auto* driver_data = static_cast<InternalData*>(device_get_driver_data(device));
    device_set_driver_data(device, nullptr);
    delete driver_data;
    return 0;
}

const I2cControllerApi esp32_i2c_api = {
    .read = read,
    .write = write,
    .write_read = write_read
};

Driver esp32_i2c_driver = {
    .name = "esp32_i2c",
    .compatible = (const char*[]) { "espressif,esp32-i2c", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = (void*)&esp32_i2c_api,
    .device_type = &I2C_CONTROLLER_TYPE,
    .internal = { 0 }
};

} // extern "C"
