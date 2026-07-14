#include <tactility/module.h>

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

struct Module elecrow_crowpanel_basic_50_module = {
    .name = "elecrow-crowpanel-basic-50",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
