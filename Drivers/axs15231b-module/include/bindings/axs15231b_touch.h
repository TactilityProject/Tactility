// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/axs15231b_touch.h>

// The devicetree compiler derives the expected config typedef name from the compatible
// string's suffix (e.g. "axs,axs15231b-touch" -> axs15231b_touch_config_dt), not from the node
// name or driver name, so the tag here must match that exactly.
DEFINE_DEVICETREE(axs15231b_touch, struct Axs15231bTouchConfig)
