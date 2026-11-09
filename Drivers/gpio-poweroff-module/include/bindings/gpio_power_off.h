// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/gpio_power_off.h>

#ifdef __cplusplus
extern "C" {
#endif

// The devicetree compiler derives the expected config typedef name from the compatible
// string ("gpio-power-off" -> gpio_power_off_config_dt), so the tag must match it.
DEFINE_DEVICETREE(gpio_power_off, struct GpioPowerOffConfig)

#ifdef __cplusplus
}
#endif
