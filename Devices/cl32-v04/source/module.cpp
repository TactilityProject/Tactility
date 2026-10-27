#include <tactility/module.h>

#include "cl32_v4_display.h"
#include "cl32_v4_light.h"
#include "cl32_v4_keyboard.h"
#include "cl32_v4_power.h"

extern "C" {

static error_t start() {
    cl32_v4_display_detect_start();
    return ERROR_NONE;
}

static error_t stop() {
    cl32_v4_display_detect_stop();
    cl32_v4_display_teardown();
    return ERROR_NONE;
}

static Driver* const cl32_drivers[] = {
    &cl32_v4_power_driver,
    &cl32_v4_power_supply_driver,
    &cl32_v4_keyboard_driver,
    &cl32_v4_light_driver,
    nullptr
};

Module cl32_v04_module = {
    .name = "cl32",
    .start = start,
    .stop = stop,
    .drivers = cl32_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
