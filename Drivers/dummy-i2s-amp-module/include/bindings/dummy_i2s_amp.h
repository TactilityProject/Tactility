// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/dummy_i2s_amp.h>

#ifdef __cplusplus
extern "C" {
#endif

// One DEFINE_DEVICETREE per compatible string -- the devicetree compiler derives the
// expected config typedef name from the compatible string's suffix (e.g. "maxim,max98357a"
// -> max98357a_config_dt), not from a name we choose, so each supported chip needs its own
// typedef even though they all share the same underlying config layout.
DEFINE_DEVICETREE(max98357a, struct DummyI2sAmpConfig)
DEFINE_DEVICETREE(ns4168, struct DummyI2sAmpConfig)

#ifdef __cplusplus
}
#endif
