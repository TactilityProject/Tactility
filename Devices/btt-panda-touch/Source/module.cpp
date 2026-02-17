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

struct Module btt_panda_touch_module = {
    .name = "btt-panda-touch",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
