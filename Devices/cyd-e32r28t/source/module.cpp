#include <tactility/module.h>

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

struct Module cyd_e32r28t_module = {
    .name = "cyd-e32r28t",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
