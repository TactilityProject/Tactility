// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver t5s3_display_driver;
extern Driver t5s3_display_bw_driver;

static Driver* const lilygo_t5s3_epd_drivers[] = {
    &t5s3_display_driver,
    &t5s3_display_bw_driver,
    nullptr
};

Module lilygo_t5s3_epd_module = {
    .name = "lilygo-t5s3-epd",
    .start = nullptr,
    .stop = nullptr,
    .drivers = lilygo_t5s3_epd_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
