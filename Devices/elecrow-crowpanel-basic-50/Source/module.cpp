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

/** @warn The variable name must be exactly "device_module" */
struct Module device_module = {
    .name = "Module",
    .start = start,
    .stop = stop
};

}
