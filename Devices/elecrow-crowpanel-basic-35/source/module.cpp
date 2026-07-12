#include <tactility/module.h>

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

Module elecrow_crowpanel_basic_35_module = {
    .name = "elecrow-crowpanel-basic-35",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
