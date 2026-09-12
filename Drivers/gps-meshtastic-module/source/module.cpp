// SPDX-License-Identifier: GPL-3.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver meshtastic_gps_driver;

static Driver* const meshtastic_generic_drivers[] = {
    &meshtastic_gps_driver,
    nullptr
};

Module gps_generic_module = {
    .name = "gps-meshtastic",
    .start = nullptr,
    .stop = nullptr,
    .drivers = meshtastic_generic_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
