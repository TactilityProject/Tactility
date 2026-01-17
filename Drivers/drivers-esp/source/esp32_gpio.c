#include <drivers-esp/esp32_gpio.h>
#include <drivers/gpio_controller.h>
#include <drivers/gpio.h>
// ESP
#include <esp_log.h>
#include <driver/gpio.h>

#define TAG "esp32_gpio"

static bool set_level(const struct device* dev, gpio_pin_t pin, bool high) {
    return gpio_set_level(pin, high) == ESP_OK;
}

static bool get_level(const struct device* dev, gpio_pin_t pin, bool* high) {
    *high = gpio_get_level(pin) != 0;
    return true;
}

static bool set_options(const struct device* dev, gpio_pin_t pin, gpio_flags_t options) {
    // TODO
    // gpio_set_direction()
    // gpio_set_pull_mode()
    return true;
}

static bool get_options(const struct device* dev, gpio_pin_t pin, gpio_flags_t* options) {
    // gpio_get_direction()
    // gpio_get_pull_mode()
    // TODO
    return true;
}

const struct esp32_gpio_api esp32_gpio_api_instance = {
    .set_level = set_level,
    .get_level = get_level,
    .set_options = set_options,
    .get_options = get_options
};

int esp32_gpio_init(const struct device* device) {
    ESP_LOGI(TAG, "init %s", device->name);
    return 0;
}

int esp32_gpio_deinit(const struct device* device) {
    ESP_LOGI(TAG, "deinit %s", device->name);
    return 0;
}
