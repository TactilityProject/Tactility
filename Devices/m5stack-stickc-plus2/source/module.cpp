#include <tactility/module.h>
#include <tactility/error.h>

#include <driver/gpio.h>

constexpr auto* TAG = "StickCPlus2";

extern "C" {

static bool init_power() {
    // CH552 applies 4 V to GPIO 0, which reduces Wi-Fi sensitivity.
    // Setting output to high adds a bias of 3.3 V and suppresses over-voltage:
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_0, 1);

    // "Hold power" pin: must be set to high to keep the device powered on:
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_4, 1);

    return true;
}

static error_t start() {
    init_power();
    return ERROR_NONE;
}

static error_t stop() {
    // Empty for now
    return ERROR_NONE;
}

Module m5stack_stickc_plus2_module = {
    .name = "m5stack-stickc-plus2",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
