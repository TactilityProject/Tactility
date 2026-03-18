// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/m5pm1.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(m5pm1, struct M5pm1Config)

#ifdef __cplusplus
}
#endif
