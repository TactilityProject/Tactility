// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/ina226.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(ina226, struct Ina226Config)

#ifdef __cplusplus
}
#endif
