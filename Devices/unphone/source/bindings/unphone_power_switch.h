#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/bindings/bindings.h>
#include <drivers/unphone_power_switch.h>

// The devicetree compiler derives the expected config typedef name from the compatible
// string's suffix (e.g. "unphone,power-switch" -> power_switch_config_dt), not from the
// node name or driver name, so the tag here must match that exactly.
DEFINE_DEVICETREE(power_switch, struct UnphonePowerSwitchConfig)

#ifdef __cplusplus
}
#endif
