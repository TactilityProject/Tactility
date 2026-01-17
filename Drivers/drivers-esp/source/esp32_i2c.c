#include "tactility/drivers/esp32_i2c.h"

#include "driver/i2c.h"

#include <esp_log.h>
#include <tactility/drivers/i2c_controller.h>

#define TAG "esp32_i2c"

#define GET_CONFIG(dev) ((struct esp32_i2c_config*)dev->config)

static bool read(struct device* dev, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout) {
    // TODO: mutex
    esp_err_t result = i2c_master_read_from_device(GET_CONFIG(dev)->port, address, data, dataSize, timeout);
    ESP_ERROR_CHECK_WITHOUT_ABORT(result);
    return result == ESP_OK;
}

static bool write(struct device* dev, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    // TODO: mutex
    esp_err_t result = i2c_master_write_to_device(GET_CONFIG(dev)->port, address, data, dataSize, timeout);
    ESP_ERROR_CHECK_WITHOUT_ABORT(result);
    return result == ESP_OK;
}

static bool write_read(struct device* dev, uint8_t address, const uint8_t* write_data, size_t write_data_size, uint8_t* read_data, size_t read_data_size, TickType_t timeout) {
    // TODO: mutex
    esp_err_t result = i2c_master_write_read_device(GET_CONFIG(dev)->port, address, write_data, write_data_size, read_data, read_data_size, timeout);
    ESP_ERROR_CHECK_WITHOUT_ABORT(result);
    return result == ESP_OK;
}

const struct esp32_i2c_api esp32_i2c_api_instance = {
    .read = read,
    .write = write,
    .write_read = write_read
};

int esp32_i2c_init(const struct device* device) {
    ESP_LOGI(TAG, "init %s", device->name);
    return 0;
}

int esp32_i2c_deinit(const struct device* device) {
    ESP_LOGI(TAG, "deinit %s", device->name);
    return 0;
}
