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

struct Module guition_jc1060p470ciwy_module = {
    .name = "guition-jc1060p470ciwy",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
