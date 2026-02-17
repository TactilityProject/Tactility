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

struct Module cyd_2432s028rv3_module = {
    .name = "cyd-2432s028rv3",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
