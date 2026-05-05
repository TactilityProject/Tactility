// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/qmi8658.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(qmi8658, struct Qmi8658Config)

#ifdef __cplusplus
}
#endif
