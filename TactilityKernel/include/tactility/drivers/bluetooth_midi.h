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

// ---- BLE MIDI child device ----

/**
 * BLE MIDI profile API (MIDI over Bluetooth Low Energy specification).
 * This API is exposed by a child device of the Bluetooth device.
 */
struct BtMidiApi {
    /**
     * Start advertising the BLE MIDI service and accept incoming connections.
     * @param[in] device the MIDI child device
     * @return ERROR_NONE on success
     */
    error_t (*start)(struct Device* device);

    /**
     * Stop the BLE MIDI service and close any active connections.
     * @param[in] device the MIDI child device
     * @return ERROR_NONE on success
     */
    error_t (*stop)(struct Device* device);

    /**
     * Send MIDI message bytes over the BLE MIDI connection.
     * @param[in] device the MIDI child device
     * @param[in] msg the raw MIDI bytes
     * @param[in] len the number of bytes
     * @return ERROR_NONE on success
     */
    error_t (*send)(struct Device* device, const uint8_t* msg, size_t len);

    /**
     * @param[in] device the MIDI child device
     * @return true when a remote device is connected
     */
    bool (*is_connected)(struct Device* device);
};

extern const struct DeviceType BLUETOOTH_MIDI_TYPE;

/** Find the first ready BLE MIDI child device. Returns NULL if unavailable. */
struct Device* bluetooth_midi_get_device(void);

error_t bluetooth_midi_start(struct Device* device);
error_t bluetooth_midi_stop(struct Device* device);
error_t bluetooth_midi_send(struct Device* device, const uint8_t* msg, size_t len);
bool    bluetooth_midi_is_connected(struct Device* device);

#ifdef __cplusplus
}
#endif
