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

struct Module elecrow_crowpanel_advance_50_module = {
    .name = "elecrow-crowpanel-advance-50",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
