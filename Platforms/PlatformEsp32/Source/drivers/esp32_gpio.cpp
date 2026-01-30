// SPDX-License-Identifier: Apache-2.0
#include <driver/gpio.h>

#include <tactility/driver.h>
#include <tactility/drivers/esp32_gpio.h>

#include <tactility/error_esp32.h>
#include <tactility/log.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>

#define TAG LOG_TAG(esp32_gpio)

#define GET_CONFIG(device) ((struct Esp32GpioConfig*)device->internal.driver_data)

extern "C" {

static error_t set_level(Device* device, gpio_pin_t pin, bool high) {
    auto esp_error = gpio_set_level(static_cast<gpio_num_t>(pin), high);
    return esp_err_to_error(esp_error);
}

static error_t get_level(Device* device, gpio_pin_t pin, bool* high) {
    *high = gpio_get_level(static_cast<gpio_num_t>(pin)) != 0;
    return ERROR_NONE;
}

static error_t set_options(Device* device, gpio_pin_t pin, gpio_flags_t options) {
    const Esp32GpioConfig* config = GET_CONFIG(device);

    if (pin >= config->gpioCount) {
        return ERROR_INVALID_ARGUMENT;
    }

    gpio_mode_t mode;
    if ((options & GPIO_DIRECTION_INPUT_OUTPUT) == GPIO_DIRECTION_INPUT_OUTPUT) {
        mode = GPIO_MODE_INPUT_OUTPUT;
    } else if (options & GPIO_DIRECTION_INPUT) {
        mode = GPIO_MODE_INPUT;
    } else if (options & GPIO_DIRECTION_OUTPUT) {
        mode = GPIO_MODE_OUTPUT;
    } else {
        return ERROR_INVALID_ARGUMENT;
    }

    const gpio_config_t esp_config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = mode,
        .pull_up_en = (options & GPIO_PULL_UP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (options & GPIO_PULL_DOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTERRUPT_FROM_OPTIONS(options),
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
        .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE
#endif
    };

    auto esp_error = gpio_config(&esp_config);
    return esp_err_to_error(esp_error);
}

static int get_options(Device* device, gpio_pin_t pin, gpio_flags_t* options) {
    gpio_io_config_t esp_config;
    if (gpio_get_io_config(static_cast<gpio_num_t>(pin), &esp_config) != ESP_OK) {
        return ERROR_RESOURCE;
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

    if (esp_config.oe_inv) {
        output |= GPIO_ACTIVE_LOW;
    }

    *options = output;
    return ERROR_NONE;
}

static error_t start(Device* device) {
    ESP_LOGI(TAG, "start %s", device->name);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    ESP_LOGI(TAG, "stop %s", device->name);
    return ERROR_NONE;
}

const static GpioControllerApi esp32_gpio_api  = {
    .set_level = set_level,
    .get_level = get_level,
    .set_options = set_options,
    .get_options = get_options
};

extern struct Module platform_module;

Driver esp32_gpio_driver = {
    .name = "esp32_gpio",
    .compatible = (const char*[]) { "espressif,esp32-gpio", nullptr },
    .startDevice = start,
    .stopDevice = stop,
    .api =  (void*)&esp32_gpio_api,
    .deviceType = nullptr,
    .owner = &platform_module,
    .driver_private = nullptr
};

} // extern "C"
