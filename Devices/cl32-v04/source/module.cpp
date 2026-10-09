#include <tactility/module.h>

#include "cl32_v4_keyboard.h"
#include "cl32_v4_power.h"

extern "C" {

static Driver* const cl32_drivers[] = {
    &cl32_v4_power_driver,
    &cl32_v4_power_supply_driver,
    &cl32_v4_keyboard_driver,
    nullptr
};

Module cl32_v04_module = {
    .name = "cl32",
    .start = nullptr,
    .stop = nullptr,
    .drivers = cl32_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
