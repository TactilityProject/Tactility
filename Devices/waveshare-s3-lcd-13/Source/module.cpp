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

struct Module waveshare_s3_lcd_13_module = {
    .name = "waveshare-s3-lcd-13",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
