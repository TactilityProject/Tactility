#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/bindings/bindings.h>
#include <drivers/xteink_x4_power.h>

DEFINE_DEVICETREE(x4_power, struct XteinkX4PowerConfig)

#ifdef __cplusplus
}
#endif
