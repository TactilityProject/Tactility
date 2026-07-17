#include <tactility/module.h>

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

Module guition_jc1060p470ciwy_module = {
    .name = "guition-jc1060p470ciwy",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
