// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;
struct DeviceType;

enum UsbClass {
    USB_HOST_CLASS_HID,
    USB_HOST_CLASS_MIDI,
    USB_HOST_CLASS_MSC,
    USB_HOST_CLASS_AUDIO,
};

struct UsbHostApi {
    /**
     * @param[in] device the USB host device
     * @param[in] usb_class the class to check
     * @param[out] enabled true when the class device is active
     * @retval ERROR_NONE on success
     */
    error_t (*is_class_enabled)(struct Device* device, enum UsbClass usb_class, bool* enabled);
};

extern const struct DeviceType USB_HOST_TYPE;

error_t usb_host_is_class_enabled(struct Device* host, enum UsbClass usb_class, bool* enabled);

#ifdef __cplusplus
}
#endif
