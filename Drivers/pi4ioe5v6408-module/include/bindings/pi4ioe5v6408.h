// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/pi4ioe5v6408.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(pi4ioe5v6408, struct Pi4ioe5v6408Config)

#ifdef __cplusplus
}
#endif
