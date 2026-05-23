#include <tactility/module.h>

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

struct Module m5stack_stackchan_module = {
    .name = "m5stack-stackchan",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
