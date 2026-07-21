// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/pdm_mic.h>

#ifdef __cplusplus
extern "C" {
#endif

// One DEFINE_DEVICETREE per compatible string -- see dummy-i2s-amp-module's bindings
// header for why this can't be unified into a single typedef name.
DEFINE_DEVICETREE(spm1423, struct PdmMicConfig)

#ifdef __cplusplus
}
#endif
