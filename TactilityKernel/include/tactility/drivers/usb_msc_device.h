// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;
struct DeviceType;

// ---- USB mass-storage device mode ----

/** Which backing storage is exposed to the USB host as a mass-storage volume. */
enum UsbMscDeviceSource {
    USB_MSC_DEVICE_SOURCE_SDMMC,
    USB_MSC_DEVICE_SOURCE_FLASH,
};

/** Fired when the exposed volume's mount state changes (host mounted/unmounted it). */
typedef void (*UsbMscDeviceMountChangedCallback)(bool mounted, void* context);

/**
 * USB mass-storage device profile API (present the board's SD card or internal flash as a USB
 * mass-storage device to a host).
 */
struct UsbMscDeviceApi {
    /**
     * Claim the USB device-mode slot and expose the given storage source as a USB mass-storage
     * volume.
     * @warning the caller must ensure the backing storage is unmounted/quiesced from local use
     *          before calling this - a source exposed to a USB host while still locally mounted
     *          risks filesystem corruption from two concurrent writers. Tactility's own flash-MSC
     *          path (see UsbTusb.cpp / UsbSettingsApp) enforces this via a dedicated reboot-into-
     *          MSC boot flow rather than unmounting live, so local access never overlaps with USB
     *          exposure; local availability is only restored by rebooting back to normal OS.
     * @param[in] device the MSC device child device
     * @param[in] source which backing storage to expose
     * @param[in] source_handle the backing storage handle: a `sdmmc_card_t*` when source is
     *            USB_MSC_DEVICE_SOURCE_SDMMC, or a pointer to a `wl_handle_t` (i.e. `wl_handle_t*`)
     *            when source is USB_MSC_DEVICE_SOURCE_FLASH - passed by address, not cast through
     *            `void*` by value, since `wl_handle_t` is a plain integer type where 0 is a valid
     *            handle and would collide with the nullptr/"no handle" check otherwise. The caller
     *            resolves this - platform-esp32 has no business knowing which wear-levelling
     *            partition Tactility mounted as /data.
     * @param[in] mount_changed_cb optional callback fired on host mount/unmount, nullable
     * @param[in] context passed back to mount_changed_cb, nullable
     * @retval ERROR_RESOURCE_BUSY if another USB device class already holds the slot
     * @retval ERROR_INVALID_ARGUMENT if source_handle is invalid for the given source
     * @retval ERROR_NONE on success
     */
    error_t (*start)(struct Device* device, enum UsbMscDeviceSource source, void* source_handle,
                     UsbMscDeviceMountChangedCallback mount_changed_cb, void* context);

    /**
     * Stop presenting as a USB mass-storage device and release the USB device-mode slot.
     * @param[in] device the MSC device child device
     * @return ERROR_NONE on success
     */
    error_t (*stop)(struct Device* device);

    /**
     * @param[in] device the MSC device child device
     * @return true when a USB host currently has the volume mounted
     */
    bool (*is_connected)(struct Device* device);
};

extern const struct DeviceType USB_MSC_DEVICE_TYPE;

/**
 * Find the first started USB MSC device child device and take a reference on it.
 * @return the device with an outstanding reference, or NULL if none is available - caller must
 *         call device_put() exactly once when done, same as device_get_first_active_by_type().
 */
struct Device* usb_msc_device_get(void);

error_t usb_msc_device_start(struct Device* device, enum UsbMscDeviceSource source, void* source_handle,
                             UsbMscDeviceMountChangedCallback mount_changed_cb, void* context);
error_t usb_msc_device_stop(struct Device* device);
bool    usb_msc_device_is_connected(struct Device* device);

/** Detail string for the most recent usb_msc_device_start() failure, or "" if none. */
const char* usb_msc_device_get_last_error(void);

#ifdef __cplusplus
}
#endif
