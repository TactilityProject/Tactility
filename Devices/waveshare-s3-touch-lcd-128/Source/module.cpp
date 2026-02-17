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

struct Module waveshare_s3_touch_lcd_128_module = {
    .name = "waveshare-s3-touch-lcd-128",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
