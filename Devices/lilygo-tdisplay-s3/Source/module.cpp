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

struct Module lilygo_tdisplay_s3_module = {
    .name = "lilygo-tdisplay-s3",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
