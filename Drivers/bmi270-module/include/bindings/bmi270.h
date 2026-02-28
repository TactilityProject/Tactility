// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/bmi270.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(bmi270, struct Bmi270Config)

#ifdef __cplusplus
}
#endif
