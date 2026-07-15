#include <tactility/module.h>

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

struct Module generic_esp32p4_module = {
    .name = "generic-esp32p4",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
