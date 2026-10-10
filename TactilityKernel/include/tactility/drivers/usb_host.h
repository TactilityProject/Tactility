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
     * Works while the host is stopped.
     * @param[in] device the USB host device
     * @param[in] usb_class the class to check
     * @param[out] supported non-null, set to true when the hardware and the device configuration support the class
     * @retval ERROR_NONE on success
     * @retval ERROR_INVALID_ARGUMENT @a usb_class is out of range
     */
    error_t (*is_class_supported)(struct Device* device, enum UsbClass usb_class, bool* supported);
    /**
     * @param[in] device the USB host device
     * @param[in] usb_class the class to check
     * @param[out] enabled non-null, set to true when the class is enabled and its driver is running
     * @retval ERROR_NONE on success
     * @retval ERROR_INVALID_ARGUMENT @a usb_class is out of range
     * @retval ERROR_INVALID_STATE the host is not started
     */
    error_t (*is_class_enabled)(struct Device* device, enum UsbClass usb_class, bool* enabled);
    /**
     * Starts or stops the detection of a class. Stopping removes the devices connected for that class.
     * Restarting the host enables all supported classes again.
     * @param[in] device the USB host device
     * @param[in] usb_class the class to change
     * @param[in] enabled true to start detecting the class
     * @retval ERROR_NONE on success, also when the class already was in the requested state
     * @retval ERROR_INVALID_ARGUMENT @a usb_class is out of range
     * @retval ERROR_NOT_SUPPORTED the class is not supported
     * @retval ERROR_INVALID_STATE the host is not started
     * @return any other error when the class failed to start or stop
     */
    error_t (*set_class_enabled)(struct Device* device, enum UsbClass usb_class, bool enabled);
};

extern const struct DeviceType USB_HOST_TYPE;

error_t usb_host_is_class_supported(struct Device* host, enum UsbClass usb_class, bool* supported);

error_t usb_host_is_class_enabled(struct Device* host, enum UsbClass usb_class, bool* enabled);

error_t usb_host_set_class_enabled(struct Device* host, enum UsbClass usb_class, bool enabled);

#ifdef __cplusplus
}
#endif
