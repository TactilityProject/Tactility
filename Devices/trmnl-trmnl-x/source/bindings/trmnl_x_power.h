#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/bindings/bindings.h>
#include <drivers/trmnl_x_power.h>

DEFINE_DEVICETREE(trmnl_x_power, struct TrmnlXPowerConfig)

#ifdef __cplusplus
}
#endif
