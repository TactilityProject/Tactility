// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/ssd1306.h>

// The devicetree compiler derives the expected config typedef name from the compatible
// string's suffix (e.g. "solomon,ssd1306" -> ssd1306_config_dt), not from the node name or
// driver name, so the tag here must match that exactly.
DEFINE_DEVICETREE(ssd1306, struct Ssd1306Config)
