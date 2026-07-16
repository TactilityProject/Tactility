#include <tactility/module.h>

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

struct Module elecrow_crowpanel_basic_28_module = {
    .name = "elecrow-crowpanel-basic-28",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
