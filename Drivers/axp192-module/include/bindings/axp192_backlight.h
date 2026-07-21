// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/axp192_backlight.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(axp192_backlight, struct Axp192BacklightConfig)

#ifdef __cplusplus
}
#endif
