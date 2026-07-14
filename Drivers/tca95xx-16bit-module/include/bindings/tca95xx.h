// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/tca95xx.h>

#ifdef __cplusplus
extern "C" {
#endif

// One DEFINE_DEVICETREE per compatible string -- the devicetree compiler derives the
// expected config typedef name from the compatible string's suffix (e.g. "ti,tca9535"
// -> tca9535_config_dt), not from a name we choose, so each supported chip needs its own
// typedef even though they share the same underlying config layout (see dummy-i2s-amp-module's
// bindings header for the same pattern).
DEFINE_DEVICETREE(tca9535, struct Tca95xxConfig)
DEFINE_DEVICETREE(tca9539, struct Tca95xxConfig)

#ifdef __cplusplus
}
#endif
