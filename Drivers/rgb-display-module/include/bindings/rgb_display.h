// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/rgb_display.h>

// The devicetree compiler derives the expected config typedef name from the compatible
// string's suffix (e.g. "espressif,esp32-rgb-display" -> esp32_rgb_display_config_dt), not
// from the node name or driver name, so the tag here must match that exactly.
DEFINE_DEVICETREE(esp32_rgb_display, struct RgbDisplayConfig)
