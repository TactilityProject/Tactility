// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;
struct DeviceType;

#define USB_MSC_MOUNT_PATH_PREFIX "/usb"

struct UsbMscApi {
    bool (*eject)(struct Device* device, const char* mount_path);
};

/**
 * A USB_HOST_MSC_TYPE device exists for each mounted USB drive.
 */
extern const struct DeviceType USB_HOST_MSC_TYPE;

/**
 * Safely eject a mounted USB drive. The device is removed shortly after.
 * @param device non-null ready USB MSC device
 * @param mount_path Full mount path (e.g. "/usb0").
 * @return true if @a device is the drive mounted at @a mount_path and it was ejected.
 */
bool usb_msc_eject(struct Device* device, const char* mount_path);

#ifdef __cplusplus
}
#endif
