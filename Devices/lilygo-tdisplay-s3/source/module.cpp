#include <tactility/module.h>

#include <driver/gpio.h>

// Board peripheral power-enable pin (display, backlight, etc). Must be asserted before the
// devicetree devices below start - kernel_init() starts all dts_modules[] (this one included)
// before constructing any dts_devices[], so doing it here in start() runs early enough.
constexpr auto POWER_ON_PIN = GPIO_NUM_15;

extern "C" {

static error_t start() {
    gpio_config_t power_signal_config = {
        .pin_bit_mask = BIT64(POWER_ON_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (gpio_config(&power_signal_config) != ESP_OK) {
        return ERROR_RESOURCE;
    }

    if (gpio_set_level(POWER_ON_PIN, 1) != ESP_OK) {
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

static error_t stop() {
    if (gpio_set_level(POWER_ON_PIN, 0) != ESP_OK) {
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

Module lilygo_tdisplay_s3_module = {
    .name = "lilygo-tdisplay-s3",
    .start = start,
    .stop = stop
};

}
