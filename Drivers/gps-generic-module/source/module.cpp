// SPDX-License-Identifier: GPL-3.0-or-later
#include <tactility/module.h>

extern "C" {

Module gps_meshtastic_module = {
    .name = "gps-meshtastic",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr
};

}
