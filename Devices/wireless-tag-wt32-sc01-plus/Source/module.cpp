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

struct Module wireless_tag_wt32_sc01_plus_module = {
    .name = "wireless-tag-wt32-sc01-plus",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
