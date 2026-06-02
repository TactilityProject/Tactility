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

extern const struct DeviceType USB_HOST_MSC_TYPE;

/**
 * Safely eject a mounted USB drive.
 * @param device non-null ready USB MSC device (from device_find_first_active_by_type).
 * @param mount_path Full mount path (e.g. "/usb0").
 * @return true if the drive was found and ejected.
 */
bool usb_msc_eject(struct Device* device, const char* mount_path);

#ifdef __cplusplus
}
#endif
