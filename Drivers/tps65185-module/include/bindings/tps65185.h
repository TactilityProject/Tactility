// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/tps65185.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(tps65185, struct Tps65185Config)

#ifdef __cplusplus
}
#endif
