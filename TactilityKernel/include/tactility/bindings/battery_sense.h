// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <tactility/drivers/battery_sense.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(battery_sense, struct BatterySenseConfig)

#ifdef __cplusplus
}
#endif
