#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver t5s3_display_driver;

static Driver* const t5s3_drivers[] = {
    &t5s3_display_driver,
    nullptr
};

Module lilygo_t5_epd47_s3_module = {
    .name = "lilygo-t5-epd47-s3",
    .start = nullptr,
    .stop = nullptr,
    .drivers = t5s3_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
