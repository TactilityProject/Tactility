// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/gps.h>

struct Device;

/**
 * Attempts to auto-detect the GPS/GNSS chipset connected via uart.
 * @return GPS_MODEL_UNKNOWN when no supported chipset responded
 */
GpsModel gps_probe(Device* uart);
