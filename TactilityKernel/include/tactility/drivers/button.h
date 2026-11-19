// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <tactility/device.h>
#include <tactility/drivers/input_codes.h>
#include <tactility/error.h>

enum ButtonValue {
    BUTTON_VALUE_RELEASED = 0,
    BUTTON_VALUE_PRESSED = 1,
};

/**
 * @brief An action emitted by a button device.
 * A button reports key codes (see input_codes.h) with a press or release value, like a Linux EV_KEY event.
 */
struct ButtonEvent {
    uint16_t code;
    enum ButtonValue value;
    /** Milliseconds since boot, see get_millis() */
    uint32_t timestamp_ms;
};

/** Number of events that are queued per subscription. Events beyond this are dropped for that subscriber. */
#define BUTTON_EVENT_QUEUE_CAPACITY 8

/**
 * @brief Caller-owned subscription node for button_subscribe(), button_unsubscribe() and button_poll().
 * @warning `internal` is private to button.cpp.
 */
struct ButtonEventSubscription {
    struct {
        struct Device* device;
        struct ButtonEvent queue[BUTTON_EVENT_QUEUE_CAPACITY];
        uint8_t head;
        uint8_t count;
        struct ButtonEventSubscription* next;
    } internal;
};

/** Number of callbacks that can be registered with button_callback_add() */
#define BUTTON_CALLBACK_CAPACITY 4

/**
 * @param[in] device the button device the event originated from
 * @param[in] event the event, only valid for the duration of the call
 * @param[in] context the pointer passed to button_callback_add()
 */
typedef void (*button_event_callback_t)(struct Device* device, const struct ButtonEvent* event, void* context);

/**
 * @brief API for button drivers.
 */
struct ButtonApi {
    /**
     * @brief Reads the debounced physical state.
     * @param[in] device the button device
     * @param[out] pressed true when the button is held down
     * @retval ERROR_NONE when the operation was successful
     */
    error_t (*is_pressed)(struct Device* device, bool* pressed);
};

/**
 * @brief Pushes an event from @a device to every subscriber of that device and calls the registered callbacks.
 * @warning Not ISR-safe (takes a mutex). Call from a task.
 * @param[in] device the button device the event originated from
 * @param[in] event the event
 */
void button_emit_event(struct Device* device, struct ButtonEvent event);

/**
 * @brief Registers @a callback for the events of every button device.
 * The callback runs on the task that emits the event (the timer daemon for gpio-button), so it must not block.
 * The registry is not locked during the call, so @a callback may add or remove callbacks.
 * @param[in] callback the function to call for each event
 * @param[in] context passed to @a callback unmodified
 * @retval ERROR_NONE on success
 * @retval ERROR_RESOURCE when BUTTON_CALLBACK_CAPACITY callbacks are registered
 */
error_t button_callback_add(button_event_callback_t callback, void* context);

/**
 * @brief Removes a callback registered with button_callback_add().
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND when @a callback was not registered
 */
error_t button_callback_remove(button_event_callback_t callback);

/**
 * @brief Reads the debounced physical state.
 * @retval ERROR_NONE when @a pressed was set
 * @retval ERROR_NOT_SUPPORTED when the driver has no ButtonApi::is_pressed
 */
error_t button_is_pressed(struct Device* device, bool* pressed);

/**
 * @brief Registers @a sub for events from @a device.
 * @param[in,out] sub subscription to register. It must stay alive and stationary until unsubscribed.
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_STATE when @a sub is already registered
 */
error_t button_subscribe(struct Device* device, struct ButtonEventSubscription* sub);

/**
 * @brief Removes a subscription registered with button_subscribe().
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND when @a sub was not registered
 */
error_t button_unsubscribe(struct Device* device, struct ButtonEventSubscription* sub);

/**
 * @brief Pops the next queued event for @a sub without blocking.
 * @retval ERROR_NONE @a out_event was filled
 * @retval ERROR_TIMEOUT nothing queued
 */
error_t button_poll(struct Device* device, struct ButtonEventSubscription* sub, struct ButtonEvent* out_event);

extern const struct DeviceType BUTTON_TYPE;

#ifdef __cplusplus
}
#endif
