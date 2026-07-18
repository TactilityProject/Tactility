#include <tactility/module.h>

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

struct Module es3c28p_module = {
    .name = "es3c28p",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
