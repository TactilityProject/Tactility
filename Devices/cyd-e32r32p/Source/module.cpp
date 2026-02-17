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

struct Module cyd_e32r32p_module = {
    .name = "cyd-e32r32p",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
