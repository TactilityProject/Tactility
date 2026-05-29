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

/** Find the first ready USB MSC device. Returns NULL if unavailable. */
struct Device* usb_host_msc_get_device(void);

/**
 * Safely eject a mounted USB drive.
 * @param mount_path Full mount path (e.g. "/usb0").
 * @return true if the drive was found and ejected.
 */
bool usb_msc_eject(const char* mount_path);

#ifdef __cplusplus
}
#endif
