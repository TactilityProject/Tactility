// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/axp2101_backlight.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(axp2101_backlight, struct Axp2101BacklightConfig)

#ifdef __cplusplus
}
#endif
