#include <tactility/module.h>

#include "cl32_detect.h"

extern "C" {

static error_t start() {
    cl32_power_detect_start();
    return ERROR_NONE;
}

static error_t stop() {
    cl32_power_detect_stop();
    cl32_teardown_devices();
    return ERROR_NONE;
}

static Driver* const cl32_drivers[] = {
    nullptr
};

Module cl32_v02_v03_module = {
    .name = "cl32",
    .start = start,
    .stop = stop,
    .drivers = cl32_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
