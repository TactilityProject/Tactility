// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <tactility/device.h>
#include <tactility/error.h>

/** The highest amount of sensing channels a touchbar can report */
#define TOUCHBAR_MAX_CHANNELS 16

/** Max events stored in the queue of a subscriber */
#define TOUCHBAR_EVENT_QUEUE_CAPACITY 16

/**
 * @brief Optional capabilities of a touchbar.
 *
 * Every touchbar reports the touch state of its channels. The rest depends on the chip:
 * a plain button controller offers none of these, a slider controller with on-chip gesture recognition offers most.
 */
enum TouchbarCapability {
    /** The touch position along the bar is known (TouchbarState::position, TouchbarEvent::position) */
    TOUCHBAR_CAPABILITY_POSITION = 1 << 0,
    /** Channels detect an object nearby before it touches (TouchbarChannel::proximity) */
    TOUCHBAR_CAPABILITY_PROXIMITY = 1 << 1,
    /** Channels report sensor counts (TouchbarChannel::counts and TouchbarChannel::reference) */
    TOUCHBAR_CAPABILITY_RAW_COUNTS = 1 << 2,
    /** The driver emits TOUCHBAR_EVENT_TAP */
    TOUCHBAR_CAPABILITY_GESTURE_TAP = 1 << 3,
    /** The driver emits TOUCHBAR_EVENT_HOLD */
    TOUCHBAR_CAPABILITY_GESTURE_HOLD = 1 << 4,
    /** The driver emits TOUCHBAR_EVENT_SWIPE */
    TOUCHBAR_CAPABILITY_GESTURE_SWIPE = 1 << 5,
    /** The driver emits TOUCHBAR_EVENT_FLICK */
    TOUCHBAR_CAPABILITY_GESTURE_FLICK = 1 << 6
};

/**
 * @brief The kind of a TouchbarEvent.
 *
 * Every touchbar emits TOUCHBAR_EVENT_TOUCH_DOWN and TOUCHBAR_EVENT_TOUCH_UP.
 * All other events depend on the capabilities of the touchbar.
 */
enum TouchbarEventType {
    /** A channel became touched (TouchbarEvent::channel) */
    TOUCHBAR_EVENT_TOUCH_DOWN,
    /** A channel was released (TouchbarEvent::channel) */
    TOUCHBAR_EVENT_TOUCH_UP,
    /**
     * The touch position changed while touched (TOUCHBAR_CAPABILITY_POSITION).
     * Consecutive moves that were not consumed yet are merged into the newest one.
     */
    TOUCHBAR_EVENT_MOVE,
    /** A short touch without movement (TOUCHBAR_CAPABILITY_GESTURE_TAP) */
    TOUCHBAR_EVENT_TAP,
    /** A long touch without movement, reported when it starts (TOUCHBAR_CAPABILITY_GESTURE_HOLD) */
    TOUCHBAR_EVENT_HOLD,
    /** A slow movement along the bar, see TouchbarEvent::direction (TOUCHBAR_CAPABILITY_GESTURE_SWIPE) */
    TOUCHBAR_EVENT_SWIPE,
    /** A fast movement along the bar, see TouchbarEvent::direction (TOUCHBAR_CAPABILITY_GESTURE_FLICK) */
    TOUCHBAR_EVENT_FLICK
};

/** The state of a single sensing channel */
struct TouchbarChannel {
    /** The channel detects an object nearby */
    bool proximity;
    /** The channel is touched */
    bool touched;
    /** Filtered raw sensor counts */
    uint16_t counts;
    /** Long term average of the counts */
    uint16_t reference;
};

/** A snapshot of the touchbar */
struct TouchbarState {
    /** Number of valid entries in channels */
    uint8_t channel_count;
    struct TouchbarChannel channels[TOUCHBAR_MAX_CHANNELS];
    /** Any of the channels is touched */
    bool touched;
    /** A hold is in progress */
    bool holding;
    /** Position of the touch along the bar, from 0 to position_max. Only meaningful while touched. (TOUCHBAR_CAPABILITY_POSITION) */
    uint16_t position;
    /** The highest value that position can have */
    uint16_t position_max;
};

/** A touchbar event */
struct TouchbarEvent {
    enum TouchbarEventType type;
    /**
     * For TOUCHBAR_EVENT_SWIPE and TOUCHBAR_EVENT_FLICK: +1 when the touch moved towards a higher position
     * and -1 when it moved towards a lower position. 0 for all other events.
     */
    int8_t direction;
    /** For TOUCHBAR_EVENT_TOUCH_DOWN and TOUCHBAR_EVENT_TOUCH_UP: the channel that changed. 0 for all other events. */
    uint8_t channel;
    /** Bit n is set when channel n is touched, after this event */
    uint16_t touch_mask;
    /**
     * Position of the touch along the bar, from 0 to TouchbarState::position_max.
     * For TOUCHBAR_EVENT_TOUCH_UP it is the position where the touch was released.
     */
    uint16_t position;
};

/**
 * @brief Caller-owned subscription for the sub/unsub touchbar API
 */
struct TouchbarEventSubscription {
    struct {
        struct Device* device;
        struct TouchbarEvent queue[TOUCHBAR_EVENT_QUEUE_CAPACITY];
        uint8_t head;
        uint8_t count;
        struct TouchbarEventSubscription* next;
    } internal;
};

/**
 * @brief API for capacitive touch bar drivers
 */
struct TouchbarApi {
    /** Bitset of TouchbarCapability values */
    uint32_t capabilities;

    /**
     * @brief Reads the most recent state that the driver received from the hardware.
     * @param[in] device the touchbar device
     * @param[out] state pointer to store the state
     * @retval ERROR_INVALID_STATE if the driver has not received any data yet
     * @retval ERROR_NONE on success
     */
    error_t (*read_state)(struct Device* device, struct TouchbarState* state);

    /**
     * @brief Recalibrates the sensing channels to the current environment.
     * May be NULL when the chip has no such function or calibrates by itself.
     * @param[in] device the touchbar device
     */
    error_t (*recalibrate)(struct Device* device);

    /**
     * @brief Optional dynamic override for capability checks.
     * @warning Nullable. When null, touchbar_has_capability() checks the static `capabilities` field
     * instead.
     * @param[in] device the touchbar device
     * @param[in] capability the bitset of TouchbarCapability value(s) being queried
     * @return true if all specified capabilities are available for this device thing
     */
    bool (*has_capability)(struct Device* device, uint32_t capability);
};

/**
 * @brief Reads the most recent state of the touchbar.
 * @retval ERROR_INVALID_STATE if the device is not started or has not received any data yet
 */
error_t touchbar_read_state(struct Device* device, struct TouchbarState* state);

/**
 * @brief Recalibrates the sensing channels to the current environment. 
 * @note Keep the bar untouched while this runs.
 * @retval ERROR_INVALID_STATE if the device is not started
 * @retval ERROR_NOT_SUPPORTED if the driver has no TouchbarApi::recalibrate
 */
error_t touchbar_recalibrate(struct Device* device);

/**
 * @param[in] device the touchbar device
 * @param[in] capability the bitset of TouchbarCapability value(s) being queried
 * @return true if all specified capabilities are available for this device
 */
bool touchbar_has_capability(struct Device* device, uint32_t capability);

/**
 * @brief Pushes an event from @a device to every current subscriber (see touchbar_subscribe()).
 * Meant for touchbar drivers, which call it from their own thread.
 * A TOUCHBAR_EVENT_MOVE replaces the newest queued event of a subscriber when that is a TOUCHBAR_EVENT_MOVE tooo
 * Other events for a full subscriber queue are dropped.
 */
void touchbar_emit_event(struct Device* device, struct TouchbarEvent event);

/**
 * @brief Registers @a sub for events from @a device
 * @param[in] device the touchbar device
 * @param[in] sub caller-owned node, must stay valid until touchbar_unsubscribe()
 * @retval ERROR_INVALID_STATE if @a sub is already subscribed
 */
error_t touchbar_subscribe(struct Device* device, struct TouchbarEventSubscription* sub);

/**
 * @brief Removes a subscription previously registered with touchbar_subscribe()
 * @retval ERROR_NOT_FOUND if @a sub is not subscribed
 */
error_t touchbar_unsubscribe(struct Device* device, struct TouchbarEventSubscription* sub);

/**
 * @brief Pops the oldest queued event of @a sub without blocking it 
 * @param[in] device the touchbar device
 * @param[in] sub a subscribed node
 * @param[out] out_event pointer to store the event
 * @retval ERROR_TIMEOUT if no event is queued
 */
error_t touchbar_poll(struct Device* device, struct TouchbarEventSubscription* sub, struct TouchbarEvent* out_event);

extern const struct DeviceType TOUCHBAR_TYPE;

#ifdef __cplusplus
}
#endif
