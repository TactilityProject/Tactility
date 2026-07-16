#include <tactility/module.h>

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

Module cyd_2432s024c_module = {
    .name = "cyd-2432s024c",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
