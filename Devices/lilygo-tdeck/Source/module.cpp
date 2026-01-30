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

/** @warn The variable name must be exactly "platform_module" */
struct Module device_module = {
    .name = "LilyGO T-Deck",
    .start = start,
    .stop = stop
};

}
