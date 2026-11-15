// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/bma530.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(bma530, struct Bma530Config)

#ifdef __cplusplus
}
#endif
