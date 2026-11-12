// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/pcf85063.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(pcf85063, struct Pcf85063Config)

#ifdef __cplusplus
}
#endif
