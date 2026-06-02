// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;
struct DeviceType;

/**
 * Decoded USB MIDI message.
 *
 * `status` encodes both message type and channel:
 *   type    = status & 0xF0  (0x80=NoteOff, 0x90=NoteOn, 0xB0=CC, 0xC0=PC, 0xE0=PitchBend, ...)
 *   channel = status & 0x0F  (0–15)
 */
typedef struct {
    uint8_t cable;  /**< USB cable number (0–15, almost always 0) */
    uint8_t status; /**< MIDI status byte */
    uint8_t data1;  /**< First data byte */
    uint8_t data2;  /**< Second data byte */
} usb_midi_message_t;

typedef void (*usb_midi_message_cb_t)(const usb_midi_message_t* msg, void* user_data);

struct UsbMidiApi {
    void (*set_callback)(struct Device* device, usb_midi_message_cb_t callback, void* user_data);
    bool (*is_connected)(struct Device* device);
};

extern const struct DeviceType USB_HOST_MIDI_TYPE;

/**
 * Register a callback for incoming MIDI messages.
 * Replaces any previously registered callback. Pass NULL to disable.
 * @param device non-null ready USB MIDI device.
 */
// TODO: Make an interface that takes/releases control
void usb_midi_set_callback(struct Device* device, usb_midi_message_cb_t callback, void* user_data);

/**
 * Returns true if a MIDI device is currently connected and streaming.
 * @param device non-null ready USB MIDI device.
 */
bool usb_midi_is_connected(struct Device* device);

#ifdef __cplusplus
}
#endif
