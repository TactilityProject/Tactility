#include <tactility/module.h>
#include <tactility/error.h>

#include <driver/gpio.h>

extern "C" {

static error_t start() {
    // Set the RGB LED pins to output and turn them off (0 on, 1 off)
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT); // Red
    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT); // Green
    gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT); // Blue

    gpio_set_level(GPIO_NUM_4, 1); // Red
    gpio_set_level(GPIO_NUM_16, 1); // Green
    gpio_set_level(GPIO_NUM_17, 1); // Blue

    return ERROR_NONE;
}

static error_t stop() {
    // Empty for now
    return ERROR_NONE;
}

struct Module cyd_2432s028r_module = {
    .name = "cyd-2432s028r",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
