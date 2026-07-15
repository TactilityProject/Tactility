#include <tactility/module.h>

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

struct Module generic_esp32c6_module = {
    .name = "generic-esp32c6",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
