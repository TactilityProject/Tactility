// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/gps.h>

struct Device;

/**
 * Sends the init sequence for a specific, already-probed GPS model over uart.
 */
bool gps_init(Device* uart, GpsModel model);
