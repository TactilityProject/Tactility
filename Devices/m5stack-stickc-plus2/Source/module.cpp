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

struct Module m5stack_stickc_plus2_module = {
    .name = "m5stack-stickc-plus2",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
