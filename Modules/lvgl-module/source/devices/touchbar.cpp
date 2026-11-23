// SPDX-License-Identifier: Apache-2.0
#include <lvgl/devices/touchbar.h>
#include <lvgl/devices/device_context.h>
#include <lvgl/devices/keyboard.h>
#include <lvgl/devices/keyboard_private.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/drivers/touchbar.h>
#include <tactility/log.h>

#include <new>

constexpr auto* TAG = "lvgl_touchbar";

// More than typical LVGL because multiple events can happen from one gesture.
constexpr uint8_t TOUCHBAR_NAV_PENDING_CAPACITY = 16;

struct LvglTouchbarContext {
    struct TouchbarEventSubscription subscription;
    struct {
        uint32_t codepoint;
        bool pressed;
    } pending[TOUCHBAR_NAV_PENDING_CAPACITY];
    uint8_t head;
    uint8_t count;
};

static void push_pending(LvglTouchbarContext* context, uint32_t codepoint, bool pressed) {
    if (context->count >= TOUCHBAR_NAV_PENDING_CAPACITY) {
        LOG_W(TAG, "Pending navigation queue full, dropping event");
        return;
    }
    const uint8_t tail = (context->head + context->count) % TOUCHBAR_NAV_PENDING_CAPACITY;
    context->pending[tail] = { .codepoint = codepoint, .pressed = pressed };
    context->count++;
}

static bool pop_pending(LvglTouchbarContext* context, KeyboardKeyData* out_key) {
    if (context->count == 0) {
        return false;
    }
    const auto& entry = context->pending[context->head];
    *out_key = KeyboardKeyData {};
    out_key->key = entry.codepoint;
    out_key->pressed = entry.pressed;
    out_key->continue_reading = context->count > 1;
    context->head = (context->head + 1) % TOUCHBAR_NAV_PENDING_CAPACITY;
    context->count--;
    return true;
}

// The bar is split into three zones. 
// Left steps back, middle selects, right steps forward, seems pretty intuitive
static uint32_t zone_codepoint(uint8_t zone) {
    switch (zone) {
        case 0: return CODEPOINT_FOCUS_PREVIOUS;
        case 1: return CODEPOINT_ENTER;
        default: return CODEPOINT_FOCUS_NEXT;
    }
}

static uint8_t zone_from_position(uint16_t position, uint16_t position_max) {
    if (position_max == 0) return 1;
    const uint32_t scaled = static_cast<uint32_t>(position) * 3;
    if (scaled < static_cast<uint32_t>(position_max)) return 0;
    if (scaled < 2u * position_max) return 1;
    return 2;
}

static uint8_t zone_from_touch_mask(uint16_t touch_mask, uint8_t channel_count) {
    if (channel_count == 0) return 1;
    for (uint8_t channel = 0; channel < channel_count; channel++) {
        if ((touch_mask & (1u << channel)) != 0) {
            return static_cast<uint8_t>(static_cast<uint32_t>(channel) * 3 / channel_count);
        }
    }
    return 1;
}

// which third of the bar a tap landed in
static uint32_t tap_codepoint(Device* device, const TouchbarEvent& event) {
    TouchbarState state;
    const bool has_state = touchbar_read_state(device, &state) == ERROR_NONE;
    if (touchbar_has_capability(device, TOUCHBAR_CAPABILITY_POSITION) && has_state) {
        return zone_codepoint(zone_from_position(event.position, state.position_max));
    }
    if (has_state) {
        return zone_codepoint(zone_from_touch_mask(event.touch_mask, state.channel_count));
    }
    // No snapshot yet: select is the safe default for a tap we can't place.
    return CODEPOINT_ENTER;
}

// Handles... events. duh.
static void handle_event(Device* device, const TouchbarEvent& event, LvglTouchbarContext* context) {
    uint32_t codepoint = 0;
    switch (event.type) {
        case TOUCHBAR_EVENT_HOLD:
            if (touchbar_has_capability(device, TOUCHBAR_CAPABILITY_GESTURE_HOLD)) {
                codepoint = CODEPOINT_ESCAPE;
            }
            break;
        case TOUCHBAR_EVENT_TAP:
            if (touchbar_has_capability(device, TOUCHBAR_CAPABILITY_GESTURE_TAP)) {
                codepoint = tap_codepoint(device, event);
            }
            break;
        case TOUCHBAR_EVENT_SWIPE:
            if (touchbar_has_capability(device, TOUCHBAR_CAPABILITY_GESTURE_SWIPE)) {
                codepoint = event.direction >= 0 ? CODEPOINT_FOCUS_NEXT : CODEPOINT_FOCUS_PREVIOUS;
            }
            break;
        case TOUCHBAR_EVENT_FLICK:
            if (touchbar_has_capability(device, TOUCHBAR_CAPABILITY_GESTURE_FLICK)) {
                codepoint = event.direction >= 0 ? CODEPOINT_FOCUS_NEXT : CODEPOINT_FOCUS_PREVIOUS;
            }
            break;
        case TOUCHBAR_EVENT_TOUCH_DOWN:
            // if the controller doesn't support the gestures and stuff, we can use it as a tap
            if (!touchbar_has_capability(device, TOUCHBAR_CAPABILITY_GESTURE_TAP)) {
                codepoint = tap_codepoint(device, event);
            }
            break;
        default:
            break;
    }

    if (codepoint != 0) {
        push_pending(context, codepoint, true);
        push_pending(context, codepoint, false);
    }
}

static void lvgl_touchbar_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* wrapper = static_cast<LvglDeviceContext*>(lv_indev_get_driver_data(indev));
    auto* context = static_cast<LvglTouchbarContext*>(wrapper->context);

    TouchbarEvent event;
    while (touchbar_poll(wrapper->device, &context->subscription, &event) == ERROR_NONE) {
        handle_event(wrapper->device, event, context);
    }

    KeyboardKeyData key_data = {};
    if (!pop_pending(context, &key_data)) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = false;
        return;
    }

    lvgl_keyboard_apply_key(indev, &key_data, data);
}

error_t lvgl_touchbar_add(Device* device, lv_display_t* display, lv_indev_t** out_indev) {
    if (device == nullptr || out_indev == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (device_get_type(device) != &TOUCHBAR_TYPE) {
        return ERROR_INVALID_ARGUMENT;
    }

    lv_indev_t* existing = lvgl_touchbar_find_by_device(device);
    if (existing != nullptr) {
        *out_indev = existing;
        return ERROR_NONE;
    }

    auto* context = new(std::nothrow) LvglTouchbarContext();
    if (context == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    *context = LvglTouchbarContext {};

    error_t error = touchbar_subscribe(device, &context->subscription);
    if (error != ERROR_NONE) {
        delete context;
        return error;
    }

    auto* wrapper = new(std::nothrow) LvglDeviceContext(context);
    if (wrapper == nullptr) {
        touchbar_unsubscribe(device, &context->subscription);
        delete context;
        return ERROR_OUT_OF_MEMORY;
    }
    wrapper->device = device;

    lv_indev_t* indev = lv_indev_create();
    if (indev == nullptr) {
        touchbar_unsubscribe(device, &context->subscription);
        delete wrapper;
        return ERROR_OUT_OF_MEMORY;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, lvgl_touchbar_read_cb);
    lv_indev_set_driver_data(indev, wrapper);
    if (display != nullptr) {
        lv_indev_set_display(indev, display);
    }
    lvgl_keyboard_enable(indev);

    *out_indev = indev;
    return ERROR_NONE;
}

void lvgl_touchbar_remove(lv_indev_t* indev) {
    if (indev == nullptr) {
        return;
    }

    auto* wrapper = static_cast<LvglDeviceContext*>(lv_indev_get_driver_data(indev));
    if (wrapper != nullptr) {
        auto* context = static_cast<LvglTouchbarContext*>(wrapper->context);
        if (context != nullptr) {
            touchbar_unsubscribe(wrapper->device, &context->subscription);
        }
    }

    lvgl_keyboard_disable(indev);
    lv_indev_delete(indev);
    delete wrapper;
}

lv_indev_t* lvgl_touchbar_find_by_device(Device* device) {
    for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev != nullptr; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) != LV_INDEV_TYPE_KEYPAD) {
            continue;
        }
        auto* wrapper = static_cast<LvglDeviceContext*>(lv_indev_get_driver_data(indev));
        if (wrapper != nullptr && wrapper->device == device && device_get_type(device) == &TOUCHBAR_TYPE) {
            return indev;
        }
    }
    return nullptr;
}
