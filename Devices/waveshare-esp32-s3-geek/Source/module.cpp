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
struct Module waveshare_esp32_s3_geek_module = {
    .name = "waveshare-esp32-s3-geek",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
