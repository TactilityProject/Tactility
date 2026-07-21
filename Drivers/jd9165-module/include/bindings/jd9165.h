// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/jd9165.h>

// The devicetree compiler derives the expected config typedef name from the compatible
// string's suffix (e.g. "jdi,jd9165" -> jd9165_config_dt), not from the node name or driver
// name, so the tag here must match that exactly.
DEFINE_DEVICETREE(jd9165, struct Jd9165Config)
