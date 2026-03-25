// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/mpu6886.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(mpu6886, struct Mpu6886Config)

#ifdef __cplusplus
}
#endif
