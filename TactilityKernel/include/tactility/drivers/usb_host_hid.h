// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;
struct DeviceType;

struct UsbHidApi {
    bool (*is_connected)(struct Device* device);
};

extern const struct DeviceType USB_HOST_HID_TYPE;

/** Find the first ready USB HID device. Returns NULL if unavailable. */
struct Device* usb_host_hid_get_device(void);

/** Returns true if any HID device (keyboard or mouse) is currently connected. */
bool usb_host_hid_is_connected(void);

#ifdef __cplusplus
}
#endif
