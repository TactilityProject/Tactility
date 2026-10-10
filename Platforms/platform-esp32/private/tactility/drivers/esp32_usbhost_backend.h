#pragma once

#include <tactility/error.h>

struct Device;

/**
 * Detects connections for one USB class while the USB host runs.
 * Creates a child device of the host for each connection and destroys it on disconnect.
 */
struct Esp32UsbHostBackend {
    /**
     * @param[in] host the USB host device
     * @param[out] out_context the backend state, passed to stop()
     */
    error_t (*start)(struct Device* host, void** out_context);
    /**
     * Destroys the connected devices, then releases the backend.
     * @retval ERROR_NONE the context was freed
     * @return any other error when the context is still in use and was retained
     */
    error_t (*stop)(void* context);
};

extern const struct Esp32UsbHostBackend esp32_usbhost_hid_backend;
extern const struct Esp32UsbHostBackend esp32_usbhost_midi_backend;
extern const struct Esp32UsbHostBackend esp32_usbhost_msc_backend;
extern const struct Esp32UsbHostBackend esp32_usbhost_uac_backend;
