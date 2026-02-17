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
struct Module cyd_e32r28t_module = {
    .name = "cyd-e32r28t",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
