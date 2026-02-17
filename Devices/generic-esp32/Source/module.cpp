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

struct Module generic_esp32_module = {
    .name = "generic-esp32",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
