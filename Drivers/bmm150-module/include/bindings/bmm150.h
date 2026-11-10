// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/bmm150.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(bmm150, struct Bmm150Config)

#ifdef __cplusplus
}
#endif
