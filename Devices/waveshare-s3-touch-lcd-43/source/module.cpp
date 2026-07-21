#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module waveshare_s3_touch_lcd_43_module = {
    .name = "waveshare-s3-touch-lcd-43",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
