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
struct Module device_module = {
    .name = "elecrow-crowpanel-basic-28",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
