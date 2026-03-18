// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/bm8563.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(bm8563, struct Bm8563Config)

#ifdef __cplusplus
}
#endif
