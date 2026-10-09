// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/error.h>

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

struct UsbMidiApi {
    bool (*is_connected)(struct Device* device);
};

extern const struct DeviceType USB_HOST_MIDI_TYPE;

struct UsbMidiEvent {
    /** Stamped by usb_midi_event_emit(); any value passed in by the caller is ignored. */
    uint64_t timestamp;
    usb_midi_message_t message;
};

/**
 * Number of events that can be queued per subscription before usb_midi_event_emit() starts
 * returning ERROR_RESOURCE (dropping the newest event, preserving FIFO order of what's
 * already queued). Sized for bursts such as chords and controller sweeps.
 */
#define USB_MIDI_EVENT_QUEUE_CAPACITY 32

/**
 * Caller-owned subscription node, registered with usb_midi_event_subscribe() and drained with
 * usb_midi_event_poll(). Events queue by value (FIFO).
 * @warning Fields other than `bit` are for internal use only; do not read or write them
 * directly.
 */
struct UsbMidiEventSubscription {
    /** Set by usb_midi_event_subscribe(). Read-only for the caller: OR it into a
     * task_event_group_wait() mask to block on this subscription. */
    uint32_t bit;

    struct {
        /** The MIDI device this subscription receives events for; set by usb_midi_event_subscribe(). */
        struct Device* device;
        /** Caller-owned, borrowed; set by usb_midi_event_subscribe(). */
        struct TaskEventGroup* event_group;
        struct UsbMidiEvent queue[USB_MIDI_EVENT_QUEUE_CAPACITY];
        uint8_t head;
        uint8_t count;
        struct UsbMidiEventSubscription* next;
    } internal;
};

/**
 * Register a subscription for incoming MIDI messages of @a device.
 * @warning Does not work in ISR context.
 * @param[in,out] sub subscription to register; owns the storage, must stay alive (and
 * stationary) until unsubscribed
 * @param[in] event_group caller-owned group to wait on; must outlive @a sub (i.e. be
 * destructed only after usb_midi_event_unsubscribe()). To block for an event, call
 * task_event_group_wait()/task_event_group_wait_any() on this group, then drain with
 * usb_midi_event_poll().
 * @param[in] device the USB MIDI device
 * @retval ERROR_NONE on success
 * @retval ERROR_RESOURCE @a event_group has no free bits left to claim; @a sub was not registered
 * @retval ERROR_INVALID_STATE @a sub is already registered
 */
error_t usb_midi_event_subscribe(struct UsbMidiEventSubscription* sub, struct TaskEventGroup* event_group, struct Device* device);

/**
 * Remove a previously registered subscription.
 * @warning Does not work in ISR context.
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND if no matching subscription exists
 */
error_t usb_midi_event_unsubscribe(struct UsbMidiEventSubscription* sub);

/**
 * Non-blocking: pop the next event for @a sub if one is already queued.
 * @retval ERROR_NONE @a out_event was filled
 * @retval ERROR_TIMEOUT nothing queued right now
 */
error_t usb_midi_event_poll(struct UsbMidiEventSubscription* sub, struct UsbMidiEvent* out_event);

/**
 * Queue @a message for every subscription of @a device. Called by USB MIDI drivers.
 * Never calls subscriber code.
 * @warning Does not work in ISR context.
 * @retval ERROR_NONE delivered to every matching subscription
 * @retval ERROR_NOT_FOUND no subscription for @a device
 * @retval ERROR_RESOURCE at least one subscription's queue was full and dropped the event
 */
error_t usb_midi_event_emit(struct Device* device, const usb_midi_message_t* message);

/**
 * Returns true if a MIDI device is currently connected and streaming.
 * @param device non-null ready USB MIDI device.
 */
bool usb_midi_is_connected(struct Device* device);

#ifdef __cplusplus
}
#endif
