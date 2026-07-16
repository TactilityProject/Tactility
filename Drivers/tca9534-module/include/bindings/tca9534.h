// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/tca9534.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(tca9534, struct Tca9534Config)

#ifdef __cplusplus
}
#endif
