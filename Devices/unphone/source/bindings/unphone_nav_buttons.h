// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/bindings/bindings.h>
#include <drivers/unphone_nav_buttons.h>

// The devicetree compiler derives the expected config typedef name from the compatible
// string's suffix (e.g. "unphone,nav-buttons" -> nav_buttons_config_dt), not from the
// node name or driver name, so the tag here must match that exactly.
DEFINE_DEVICETREE(nav_buttons, struct UnphoneNavButtonsConfig)

#ifdef __cplusplus
}
#endif
