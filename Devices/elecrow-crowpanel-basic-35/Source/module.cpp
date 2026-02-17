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

struct Module elecrow_crowpanel_basic_35_module = {
    .name = "elecrow-crowpanel-basic-35",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
