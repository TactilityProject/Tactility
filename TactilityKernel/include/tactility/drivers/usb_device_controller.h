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

// ---- USB device-mode (peripheral) controller ----

/**
 * Which USB device-mode class currently owns the single TinyUSB device-mode slot.
 * Only one class may be active at a time - see usb_device_controller_claim(). CDC is not part of
 * this enum: it's an addon that composites into whichever primary class is active (or stands
 * alone with none active) rather than a primary itself - see usb_cdc_device.h.
 */
enum UsbDeviceClass {
    USB_DEVICE_CLASS_NONE,
    USB_DEVICE_CLASS_MSC,
    USB_DEVICE_CLASS_HID_KEYBOARD,
    USB_DEVICE_CLASS_MIDI,
};

/**
 * A block of raw USB interface-descriptor bytes contributed by one class (MSC/HID/MIDI/CDC) to
 * the composite configuration descriptor the controller assembles at claim() time.
 *
 * Interface and endpoint numbers inside the descriptor bytes must already be renumbered by the
 * contributor to the base values the controller handed back via usb_device_controller_allocate_interfaces()
 * - the controller does not parse or patch the bytes, it only concatenates them behind the
 * config-descriptor header it writes itself.
 *
 * `hs_descriptor_bytes`/`hs_descriptor_bytes_len` are optional (leave both NULL/0 if this
 * contributor's bytes don't differ between full-speed and high-speed, e.g. HID/MIDI/CDC's
 * endpoint sizes are already speed-independent for their traffic) - only used when
 * TUD_OPT_HIGH_SPEED is set, mirroring the fs_/hs_configuration_descriptor split
 * tinyusb_config_t itself has (see MSC, whose bulk endpoint max-packet-size legitimately differs:
 * 64 bytes FS vs 512 bytes HS).
 */
struct UsbInterfaceContribution {
    const uint8_t* descriptor_bytes;   // one or more TUD_*_DESCRIPTOR blocks, back to back (full-speed)
    size_t descriptor_bytes_len;
    const uint8_t* hs_descriptor_bytes;    // optional high-speed variant; NULL to reuse descriptor_bytes
    size_t hs_descriptor_bytes_len;        // must equal descriptor_bytes_len (same interface/endpoint layout, only sizes differ)
    uint8_t interface_count;           // interfaces consumed (MSC/HID/CDC=1, MIDI=2)
    uint8_t in_endpoint_count;         // IN endpoints consumed (excluding EP0)
    uint8_t out_endpoint_count;        // OUT endpoints consumed
};

/**
 * Interface/endpoint numbers assigned to one contributor by the controller before it built its
 * descriptor bytes. Endpoint numbers are direction-local (IN and OUT each number from 1), as is
 * standard for USB - usb_device_controller_allocate_interfaces() hands out the next free *pair*
 * per direction, not a single shared counter.
 */
struct UsbInterfaceAllocation {
    uint8_t first_interface_number;
    uint8_t first_in_endpoint;   // e.g. 0x81, 0x82, ... or 0 if in_endpoint_count was 0
    uint8_t first_out_endpoint;  // e.g. 0x01, 0x02, ... or 0 if out_endpoint_count was 0
};

/**
 * A fully-described primary class descriptor plus device/string descriptor metadata, submitted
 * to claim(). CDC (if enabled) is appended by the controller itself - primary contributors never
 * see or reference CDC's interface numbers.
 */
struct UsbDeviceClaimConfig {
    // Actually a `tusb_desc_device_t*` (TinyUSB's device descriptor struct) - void* here so this
    // kernel header doesn't need to depend on TinyUSB's own headers; the controller and every
    // contributor already build against TinyUSB directly and cast accordingly. Mutable: the
    // controller patches the class triad (bDeviceClass/SubClass/Protocol) in place at claim()
    // time depending on whether CDC is composited in.
    void* device_descriptor;
    const char* const* string_descriptor;
    size_t string_descriptor_count;
    struct UsbInterfaceContribution primary; // MSC or HID or MIDI's contribution
};

/**
 * Shared owner of the TinyUSB device-mode (peripheral) stack. Exactly one primary USB device
 * class (mass storage, HID, MIDI) may be installed at a time - callers claim the slot before use
 * and release it when done. CDC is a separate, orthogonal addon (see usb_cdc_device.h) that the
 * controller composites into whichever primary is active, independent of the claim/release cycle.
 * This exists so multiple independent drivers (MSC, HID, MIDI, CDC) can share the single
 * underlying `tinyusb_driver_install()` call, composite descriptor assembly, and any
 * board-specific PHY routing without needing to know about each other.
 */
struct UsbDeviceControllerApi {
    /**
     * Starts a new interface/endpoint allocation session, resetting the numbering counters
     * allocate_interfaces() hands out. Call once, before building any descriptor bytes, as the
     * first step of a claim() attempt (i.e. before the primary class's own allocate_interfaces()
     * call) - claim() itself calls this again internally for CDC's allocation, so primary
     * contributors only need to call it for their own single call.
     *
     * There is no explicit abort/cancel: a session abandoned after begin_claim() (e.g. a
     * contributor's own allocate_interfaces() call fails and it returns early without calling
     * claim()) is simply reset by the next begin_claim() call, which unconditionally reinitializes
     * the allocation state regardless of whether the previous session ever finished.
     *
     * @param[in] device the USB device controller device
     * @retval ERROR_RESOURCE_BUSY if a different class already holds the slot
     * @retval ERROR_NONE on success
     */
    error_t (*begin_claim)(struct Device* device);

    /**
     * Ask the controller for the next free interface number and endpoint pair, before building
     * descriptor bytes. Call once per contributor (primary class, and CDC internally) per
     * begin_claim() session, in the order the resulting descriptor should list interfaces:
     * primary class first, then CDC (the controller enforces this order internally for CDC;
     * primary contributors just call this once for themselves, after begin_claim()).
     * @param[in] device the USB device controller device
     * @param[in] interface_count number of interfaces this contributor needs
     * @param[in] in_endpoint_count number of IN endpoints needed (0 if none)
     * @param[in] out_endpoint_count number of OUT endpoints needed (0 if none)
     * @param[out] out_allocation the assigned numbers
     * @retval ERROR_INVALID_STATE if called without a preceding begin_claim()
     * @retval ERROR_NONE on success
     */
    error_t (*allocate_interfaces)(struct Device* device, uint8_t interface_count,
                                    uint8_t in_endpoint_count, uint8_t out_endpoint_count,
                                    struct UsbInterfaceAllocation* out_allocation);

    /**
     * Claim the USB device-mode slot for the given primary class and install the composite
     * descriptor (primary + CDC, if the board's usbdevicecdc0 child is enabled). Must be called
     * after begin_claim() and the primary's own allocate_interfaces() call, using the same
     * device-mode session (no other claim()/begin_claim() calls in between).
     * @param[in] device the USB device controller device
     * @param[in] usb_class the class to claim the slot for
     * @param[in] config the primary class's descriptor contribution and metadata
     * @retval ERROR_RESOURCE_BUSY if a different class already holds the slot
     * @retval ERROR_NONE on success
     */
    error_t (*claim)(struct Device* device, enum UsbDeviceClass usb_class,
                      const struct UsbDeviceClaimConfig* config);

    /**
     * Release the USB device-mode slot. Stops the CDC console (if it was composited in),
     * disconnects from the host, uninstalls the TinyUSB driver, and restores any board-specific
     * PHY routing. Only the current holder may release.
     * @param[in] device the USB device controller device
     * @param[in] usb_class the class releasing the slot; must match the current holder
     * @retval ERROR_INVALID_STATE if usb_class does not hold the slot
     * @retval ERROR_NONE on success
     */
    error_t (*release)(struct Device* device, enum UsbDeviceClass usb_class);

    /**
     * @param[in] device the USB device controller device
     * @return the class currently holding the slot, or USB_DEVICE_CLASS_NONE if free
     */
    enum UsbDeviceClass (*get_active_class)(struct Device* device);

    /**
     * @param[in] device the USB device controller device
     * @return true if a usbdevicecdc0 child device is present and enabled on this board
     */
    bool (*is_cdc_enabled)(struct Device* device);
};

extern const struct DeviceType USB_DEVICE_CONTROLLER_TYPE;

/**
 * Find the first started USB device controller and take a reference on it.
 * @return the device with an outstanding reference, or NULL if none is available - caller must
 *         call device_put() exactly once when done, same as device_get_first_active_by_type().
 */
struct Device* usb_device_controller_get(void);

error_t usb_device_controller_begin_claim(struct Device* device);
error_t usb_device_controller_allocate_interfaces(struct Device* device, uint8_t interface_count,
                                                   uint8_t in_endpoint_count, uint8_t out_endpoint_count,
                                                   struct UsbInterfaceAllocation* out_allocation);
error_t usb_device_controller_claim(struct Device* device, enum UsbDeviceClass usb_class,
                                     const struct UsbDeviceClaimConfig* config);

/** Detail string (e.g. an esp_err_t name) for the most recent claim() failure, or "" if none. */
const char* usb_device_controller_get_last_error(void);
error_t usb_device_controller_release(struct Device* device, enum UsbDeviceClass usb_class);
enum UsbDeviceClass usb_device_controller_get_active_class(struct Device* device);
bool usb_device_controller_is_cdc_enabled(struct Device* device);

#ifdef __cplusplus
}
#endif
