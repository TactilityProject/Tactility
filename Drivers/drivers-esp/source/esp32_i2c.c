#include "drivers-esp/esp32_i2c.h"
#include <drivers/i2c_controller.h>
#include <esp_log.h>
#include <stdio.h>

#define TAG "esp32_i2c"

#define GET_CONFIG(dev) static_cast<struct i2c_esp_config>(dev->config)

static bool master_read(struct device* dev, uint8_t address, uint8_t* data, size_t dataSize) {
    return true;
}

static bool master_write(struct device* dev, uint8_t address, const uint8_t* data, uint16_t dataSize) {
    return true;
}

const struct esp32_i2c_api esp32_i2c_api_instance = {
    .master_read = master_read,
    .master_write = master_write
};

int esp32_i2c_init(const struct device* device) {
    ESP_LOGI(TAG, "init %s", device->name);
    return 0;
}

int esp32_i2c_deinit(const struct device* device) {
    ESP_LOGI(TAG, "deinit %s", device->name);
    return 0;
}
