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

struct Module lilygo_tdeck_pro_module = {
    .name = "lilygo-tdeck-pro",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
