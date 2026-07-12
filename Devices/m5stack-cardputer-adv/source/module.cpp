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

struct Module m5stack_cardputer_adv_module = {
    .name = "m5stack-cardputer-adv",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
