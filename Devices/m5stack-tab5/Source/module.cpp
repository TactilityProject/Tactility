#include <tactility/module.h>

extern "C" {

static error_t start() {
    // Empty for now
    return ERROR_NONE;
}

static error_t stop() {
    // Empty for now
    return ERROR_NONE;
}

/** @warning The variable name must be exactly "device_module" */
Module m5stack_tab5_module = {
    .name = "m5stack-tab5",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
