// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/ili9881c.h>

// The devicetree compiler derives the expected config typedef name from the compatible
// string's suffix (e.g. "ilitek,ili9881c" -> ili9881c_config_dt), not from the node name or
// driver name, so the tag here must match that exactly.
DEFINE_DEVICETREE(ili9881c, struct Ili9881cConfig)
