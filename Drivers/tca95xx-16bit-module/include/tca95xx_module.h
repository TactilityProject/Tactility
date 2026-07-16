// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/module.h>

#ifdef __cplusplus
extern "C" {
#endif

// The devicetree compiler derives the expected module symbol name from this component's
// folder name ("tca95xx-16bit-module" -> tca95xx_16bit_module), not from a name we choose.
extern struct Module tca95xx_16bit_module;

#ifdef __cplusplus
}
#endif
