#include <tactility/drivers/esp32_gpio.h>
#include <tactility/drivers/gpio_controller.h>

#include <driver/gpio.h>
#include <esp_log.h>

#define TAG "esp32_gpio"

#define GET_CONFIG(dev) ((struct esp32_gpio_config*)dev->config)

static bool set_level(const struct device* dev, gpio_pin_t pin, bool high) {
    return gpio_set_level(pin, high) == ESP_OK;
}

static bool get_level(const struct device* dev, gpio_pin_t pin, bool* high) {
    *high = gpio_get_level(pin) != 0;
    return true;
}

static bool set_options(const struct device* dev, gpio_pin_t pin, gpio_flags_t options) {
    const struct esp32_gpio_config* config = GET_CONFIG(dev);

    if (pin >= config->gpio_count) {
        return false;
    }

    gpio_mode_t mode;
    if (options & (GPIO_DIRECTION_INPUT_OUTPUT)) {
        mode = GPIO_MODE_INPUT_OUTPUT;
    } else if (options & GPIO_DIRECTION_INPUT) {
        mode = GPIO_MODE_INPUT;
    } else if (options & GPIO_DIRECTION_OUTPUT) {
        mode = GPIO_MODE_OUTPUT;
    } else {
        assert(false);
    }

    const gpio_config_t esp_config = {
        .pin_bit_mask = 1UL << pin,
        .mode = mode,
        .pull_up_en = (options & GPIO_PULL_UP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (options & GPIO_PULL_DOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTERRUPT_FROM_OPTIONS(options),
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
        .hys_ctrl_mode = GPIO_HYS_CTRL_EFUSE
#endif
    };

    return gpio_config(&esp_config) == ESP_OK;
}

static bool get_options(const struct device* dev, gpio_pin_t pin, gpio_flags_t* options) {
    gpio_io_config_t esp_config;
    if (gpio_get_io_config((gpio_num_t)pin, &esp_config) != ESP_OK) {
        return false;
    }

    gpio_flags_t output = 0;

    if (esp_config.pu) {
        output |= GPIO_PULL_UP;
    }

    if (esp_config.pd) {
        output |= GPIO_PULL_DOWN;
    }

    if (esp_config.ie) {
        output |= GPIO_DIRECTION_INPUT;
    }

    if (esp_config.oe) {
        output |= GPIO_DIRECTION_OUTPUT;
    }

    if (esp_config.oe) {
        output |= GPIO_DIRECTION_OUTPUT;
    }

    if (esp_config.oe_inv) {
        output |= GPIO_ACTIVE_LOW;
    }

    *options = output;
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
