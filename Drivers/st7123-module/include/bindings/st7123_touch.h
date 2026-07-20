// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/st7123_touch.h>

// The devicetree compiler derives the expected config typedef name from the compatible
// string's suffix, with hyphens replaced by underscores (e.g. "sitronix,st7123-touch" ->
// st7123_touch_config_dt), not from the node name or driver name, so the tag here must match
// that exactly.
DEFINE_DEVICETREE(st7123_touch, struct St7123TouchConfig)
