#include <tactility/module.h>

#include "cl32_detect.h"
#include "cl32_v4_keyboard.h"
#include "cl32_v4_power.h"

extern "C" {

static error_t start() {
    cl32_power_detect_start();
    return ERROR_NONE;
}

static error_t stop() {
    cl32_power_detect_stop();
    return ERROR_NONE;
}

static Driver* const cl32_drivers[] = {
    &cl32_v4_power_driver,
    &cl32_v4_power_supply_driver,
    &cl32_v4_keyboard_driver,
    nullptr
};

Module cl32_module = {
    .name = "cl32",
    .start = start,
    .stop = stop,
    .drivers = cl32_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
