// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/device_listener.h>

#ifdef __cplusplus
extern "C" {
#endif

// Internal to TactilityKernel: invoked by device lifecycle code (device_start/device_stop) to
// notify all registered listeners of a device state transition. Not part of the public API -
// external code only sees device_listener_add()/device_listener_remove().
void device_listener_notify(struct Device* dev, enum DeviceEvent event);

#ifdef __cplusplus
}
#endif
