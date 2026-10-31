#include <lvgl/devices/display.h>
#include <lvgl/devices/device_context.h>
#include <lvgl/devices/keyboard.h>
#include <lvgl/devices/mouse.h>
#include <lvgl/devices/pointer.h>
#include <lvgl/lvgl.h>
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/device_listener.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/drivers/mouse.h>
#include <tactility/drivers/pointer.h>
#include <tactility/drivers/touch.h>
#include <tactility/log.h>

#include <tactility/Memory.hpp>

#include <lvgl.h>

#include <lvgl/theme_private.h>

#include <cstddef>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

constexpr auto* TAG = "lvgl";

// Boards are not expected to expose more devices of a single type than this; device_for_each_of_type()
// callbacks run under the device ledger lock, so devices are collected here and processed afterwards.
constexpr auto LVGL_DEVICES_MAX_PER_TYPE = 8;
// Pointer and touch devices are collected in one list
constexpr auto LVGL_DEVICES_MAX_COMBINED_INPUT = LVGL_DEVICES_MAX_PER_TYPE * 2;

struct LvglDeviceList {
    struct Device* devices[LVGL_DEVICES_MAX_COMBINED_INPUT];
    size_t count;
    size_t limit;
};

/**
 * Fires periodically while a slow-refresh display (e.g. e-paper) is bound. Once
 * lv_display_get_inactive_time(), the time since the last real input event tracked in lv_indev.c,
 * crosses this threshold, invalidates the whole active screen so LVGL redraws every widget
 * through its own draw_bitmap() calls, clearing ghosting that normal partial redraws leave behind
 * over time. Checking real input activity rather than time since last draw means a redraw during
 * active use, such as a blinking cursor, never resets the timer, so this only ever acts once
 * genuinely idle.
 */
constexpr uint32_t IDLE_REPAINT_MS = 5000;

constexpr uint32_t ACTIVITY_MARGIN_MS = 20;

struct IdleRepaintContext {
    lv_display_t* lvgl_display;
    struct Device* kernel_display_device;
    lv_timer_t* timer;
    // Tick of the last input that the flags below already account for
    uint32_t handled_activity_at;
    // Set once the invalidate+lv_refr_now() repaint has run for the current idle stretch, so it
    // isn't redone every tick while still idle.
    bool repaint_done_since_activity;
    // Set only once display_refresh() itself returns ERROR_NONE, tracked separately from
    // repaint_done_since_activity so a failed refresh (e.g. ERROR_RESOURCE) keeps getting retried
    // on later idle ticks instead of being silently skipped for the rest of the idle stretch.
    bool refresh_done_since_activity;
};

// One slot per display bound in lvgl_devices_attach()'s loop below that turns out to be
// DISPLAY_CAPABILITY_SLOW_REFRESH; a board with multiple such displays needs independent idle
// tracking for each; slots are 1:1 with idle_repaint_contexts' index, not a compacted list.
struct IdleRepaintContexts {
    IdleRepaintContext slots[LVGL_DEVICES_MAX_PER_TYPE] = {};
};

// Allocated once in external memory when available
static IdleRepaintContext* idle_repaint_contexts() {
    static IdleRepaintContexts* instance = tt::newObject<IdleRepaintContexts>();
    check(instance != nullptr);
    return instance->slots;
}

extern "C" {

static bool lvgl_device_list_collect(struct Device* device, void* context) {
    struct LvglDeviceList* list = (struct LvglDeviceList*)context;
    if (list->count < list->limit) {
        list->devices[list->count++] = device;
    }
    return true;
}

// Only touched with the LVGL lock held. Stops a listener callback that was already in flight
// when lvgl_devices_detach() ran from binding a device after the indevs were torn down.
static bool devices_attached = false;

static lv_indev_t* lvgl_find_indev_by_device(struct Device* device) {
    for (lv_indev_t* indev = lv_indev_get_next(NULL); indev != NULL; indev = lv_indev_get_next(indev)) {
        auto* context = static_cast<LvglDeviceContext*>(lv_indev_get_driver_data(indev));
        if (context != nullptr && context->device == device) {
            return indev;
        }
    }
    return nullptr;
}

static void lvgl_bind_input_device(struct Device* device, lv_display_t* display) {
    const struct DeviceType* type = device_get_type(device);
    if (type == &KEYBOARD_TYPE) {
        lv_indev_t* indev;
        if (lvgl_keyboard_add(device, display, &indev) != ERROR_NONE) {
            LOG_E(TAG, "Failed to bind %s to LVGL", device->name);
        }
    } else if (type == &MOUSE_TYPE) {
        lv_indev_t* indev;
        if (lvgl_mouse_add(device, display, &indev) != ERROR_NONE) {
            LOG_E(TAG, "Failed to bind %s to LVGL", device->name);
        }
    } else if (type == &POINTER_TYPE || type == &TOUCH_TYPE) {
        if (device_get_driver(device)->api == NULL || lvgl_find_indev_by_device(device) != nullptr) {
            return;
        }
        lv_indev_t* indevs[LVGL_POINTER_MAX_SLOTS];
        if (lvgl_pointer_add(device, display, LVGL_POINTER_MAX_SLOTS, indevs) != ERROR_NONE) {
            LOG_E(TAG, "Failed to bind %s to LVGL", device->name);
        }
    }
}

static void lvgl_unbind_input_device(struct Device* device) {
    lv_indev_t* indev = lvgl_find_indev_by_device(device);
    if (indev == nullptr) {
        return;
    }
    const struct DeviceType* type = device_get_type(device);
    if (type == &KEYBOARD_TYPE) {
        lvgl_keyboard_remove(indev);
    } else if (type == &MOUSE_TYPE) {
        lvgl_mouse_remove(indev);
    } else if (type == &POINTER_TYPE || type == &TOUCH_TYPE) {
        lvgl_pointer_remove(indev);
    }
}

// Binds input devices that start while LVGL runs (e.g. a Bluetooth or USB keyboard or mouse).
// Detaches on STOPPING because the device's resources are gone once STOPPED fires.
static void lvgl_device_listener(struct Device* device, enum DeviceEvent event, void* context) {
    (void)context;
    const struct DeviceType* type = device_get_type(device);
    if (type != &KEYBOARD_TYPE && type != &MOUSE_TYPE && type != &POINTER_TYPE && type != &TOUCH_TYPE) {
        return;
    }
    if (event != DEVICE_EVENT_STARTED && event != DEVICE_EVENT_STOPPING) {
        return;
    }

    lvgl_lock();
    if (devices_attached) {
        if (event == DEVICE_EVENT_STARTED) {
            lvgl_bind_input_device(device, lv_display_get_default());
        } else {
            lvgl_unbind_input_device(device);
        }
    }
    lvgl_unlock();
}

// The tick of the last input, which the display only exposes as time since then.
// The two tick reads can differ when the task is preempted between them, so callers compare with ACTIVITY_MARGIN_MS.
static uint32_t get_activity_tick(lv_display_t* display) {
    return lv_tick_get() - lv_display_get_inactive_time(display);
}

static void idle_repaint_timer_cb(lv_timer_t* timer) {
    auto* ctx = static_cast<IdleRepaintContext*>(lv_timer_get_user_data(timer));
    uint32_t inactive_ms = lv_display_get_inactive_time(ctx->lvgl_display);
    uint32_t activity_at = lv_tick_get() - inactive_ms;
    // Any difference beyond the margin is new input, also when the tick counter wrapped in between
    const uint32_t activity_delta = activity_at - ctx->handled_activity_at;
    if (activity_delta > ACTIVITY_MARGIN_MS && activity_delta < 0U - ACTIVITY_MARGIN_MS) {
        ctx->handled_activity_at = activity_at;
        ctx->repaint_done_since_activity = false;
        ctx->refresh_done_since_activity = false;
    }
    if (inactive_ms < IDLE_REPAINT_MS) {
        return;
    }
    if (!ctx->repaint_done_since_activity) {
        ctx->repaint_done_since_activity = true;
        lv_obj_invalidate(lv_display_get_screen_active(ctx->lvgl_display));
        // lv_refr_now() runs the redraw synchronously, so display_refresh() below sees the
        // now-current framebuffer instead of stale content from before the invalidate.
        lv_refr_now(ctx->lvgl_display);
    }
    if (ctx->refresh_done_since_activity) {
        return;
    }
    // Under PARTIAL render mode the repaint above still tiles into ordinary sub-threshold
    // draw_bitmap() calls that stay in fast MODE_DU, which alone never fully clears prior
    // ghosting even when it draws the correct pixels. This forces the quality pass that clears
    // it. Tracked separately from the repaint so a failure here (e.g. ERROR_RESOURCE) retries on
    // the next idle tick instead of also repeating the repaint, which succeeded already.
    if (display_refresh(ctx->kernel_display_device) == ERROR_NONE) {
        ctx->refresh_done_since_activity = true;
    }
}

void lvgl_devices_attach() {
    lvgl_lock();

    lv_disp_t* lvgl_display = NULL;
    bool display_updates_slowly = false;

    struct LvglDeviceList display_devices = { .count = 0, .limit = LVGL_DEVICES_MAX_PER_TYPE };
    device_for_each_of_type(&DISPLAY_TYPE, &display_devices, lvgl_device_list_collect);
    for (size_t i = 0; i < display_devices.count; i++) {
        struct Device* kernel_display_device = display_devices.devices[i];

        // Placeholder drivers (boards not yet migrated to the kernel display driver) register with a
        // NULL api: they exist so the devicetree node resolves, but have nothing for LVGL to bind to.
        if (device_get_driver(kernel_display_device)->api == NULL) {
            continue;
        }

        uint16_t vres = display_get_resolution_y(kernel_display_device);
        enum DisplayColorFormat color_format = display_get_color_format(kernel_display_device);
        bool swap_bytes = color_format == DISPLAY_COLOR_FORMAT_RGB565_SWAPPED ||
            color_format == DISPLAY_COLOR_FORMAT_BGR565_SWAPPED ||
            color_format == DISPLAY_COLOR_FORMAT_BGR565;
        bool display_requires_full_frame = display_has_capability(kernel_display_device, DISPLAY_CAPABILITY_REQUIRES_FULL_FRAME);
        // Without CAP_SWAP_XY the driver can't rotate 90/270 in hardware (display_swap_xy() is
        // null and silently skipped by lvgl_display_apply_rotation()). LVGL would still switch
        // its own logical w/h for those rotations, mismatching the panel's fixed physical
        // orientation (e.g. RGB/DPI panels, whose video timing is fixed at panel-init time).
        // sw_rotate makes LVGL rotate the rendered pixels in software instead, so the driver
        // itself is never asked to do something it can't.
        bool can_hw_rotate = display_has_capability(kernel_display_device, DISPLAY_CAPABILITY_CAP_SWAP_XY) &&
            display_has_capability(kernel_display_device, DISPLAY_CAPABILITY_CAP_MIRROR);
        bool prefer_external_ram_buffer = display_has_capability(kernel_display_device, DISPLAY_CAPABILITY_PREFER_EXTERNAL_RAM);
        struct LvglDisplayConfig lvgl_display_config = {
            .buffer_height = (uint16_t)(vres > 10 ? vres / 10 : vres),
            .double_buffer = false,
            .sw_rotate = !can_hw_rotate,
            .swap_bytes = swap_bytes,
            .force_full_frame = display_requires_full_frame,
            .prefer_external_ram = prefer_external_ram_buffer
        };
        lv_disp_t* added_display = NULL;
        if (lvgl_display_add(kernel_display_device, &lvgl_display_config, &added_display) == ERROR_NONE) {
            LOG_I(TAG, "Bound %s to LVGL", kernel_display_device->name);
            // Slow-refresh panels accumulate ghosting from normal partial redraws over time, and
            // worse from content an app drew directly via draw_bitmap() outside LVGL's own screen
            // model, which LVGL has no way to know needs repainting. This function runs both at
            // boot and whenever an app that stopped LVGL closes, so clearing here covers both
            // cases without extra plumbing at app-close time specifically.
            if (display_has_capability(kernel_display_device, DISPLAY_CAPABILITY_SLOW_REFRESH)) {
                // The first attach follows the boot logo, which the first LVGL render overdraws completely
                static bool first_attach = true;
                if (!first_attach) {
                    display_clear(kernel_display_device);
                }
                first_attach = false;
                IdleRepaintContext* ctx = &idle_repaint_contexts()[i];
                if (ctx->timer == nullptr) {
                    ctx->lvgl_display = added_display;
                    ctx->kernel_display_device = kernel_display_device;
                    // The panel was just cleared or fully drawn, so the first idle stretch needs no repaint
                    ctx->handled_activity_at = get_activity_tick(added_display);
                    ctx->repaint_done_since_activity = true;
                    ctx->refresh_done_since_activity = true;
                    ctx->timer = lv_timer_create(idle_repaint_timer_cb, 1000, ctx);
                }
            }
            // Pointers/keyboards below bind to the first display bound here, matching that display's
            // refresh behavior.
            if (lvgl_display == NULL) {
                lvgl_display = added_display;
                display_updates_slowly = display_has_capability(kernel_display_device, DISPLAY_CAPABILITY_SLOW_REFRESH);
            }
        } else {
            LOG_E(TAG, "Failed to bind %s to LVGL", kernel_display_device->name);
        }
    }

    struct LvglDeviceList pointer_devices = { .count = 0, .limit = LVGL_DEVICES_MAX_COMBINED_INPUT };
    device_for_each_of_type(&POINTER_TYPE, &pointer_devices, lvgl_device_list_collect);
    device_for_each_of_type(&TOUCH_TYPE, &pointer_devices, lvgl_device_list_collect);
    for (size_t i = 0; i < pointer_devices.count; i++) {
        struct Device* kernel_pointer_device = pointer_devices.devices[i];
        if (device_get_driver(kernel_pointer_device)->api == NULL) {
            continue;
        }

        // Each physical touch device gets up to LVGL_POINTER_MAX_SLOTS independent indevs (a
        // "pool", see lvgl_pointer_add()) so LVGL can track that many simultaneous touch points -
        // touch controllers report multiple points but expose no per-point hardware max, so this
        // is a fixed ceiling rather than a per-device query.
        lv_indev_t* lvgl_pointer_slots[LVGL_POINTER_MAX_SLOTS];
        if (lvgl_pointer_add(kernel_pointer_device, lvgl_display, LVGL_POINTER_MAX_SLOTS, lvgl_pointer_slots) == ERROR_NONE) {
            LOG_I(TAG, "Bound %s to LVGL (%d touch slots)", kernel_pointer_device->name, (int)LVGL_POINTER_MAX_SLOTS);
            // Slow panels cause taps to be missed due to the long update time, prevent that
            if (display_updates_slowly) {
                for (uint8_t slot = 0; slot < LVGL_POINTER_MAX_SLOTS; slot++) {
                    lv_indev_set_long_press_time(lvgl_pointer_slots[slot], 2000);
                }
            }
        } else {
            LOG_E(TAG, "Failed to bind %s to LVGL", kernel_pointer_device->name);
        }
    }

    struct LvglDeviceList keyboard_devices = { .count = 0, .limit = LVGL_DEVICES_MAX_PER_TYPE };
    device_for_each_of_type(&KEYBOARD_TYPE, &keyboard_devices, lvgl_device_list_collect);
    for (size_t i = 0; i < keyboard_devices.count; i++) {
        struct Device* kernel_keyboard_device = keyboard_devices.devices[i];
        lv_indev_t* lvgl_keyboard_device;
        if (lvgl_keyboard_add(kernel_keyboard_device, lvgl_display, &lvgl_keyboard_device) == ERROR_NONE) {
            LOG_I(TAG, "Bound %s to LVGL", kernel_keyboard_device->name);
        } else {
            LOG_E(TAG, "Failed to bind %s to LVGL", kernel_keyboard_device->name);
        }
    }

    struct LvglDeviceList mouse_devices = { .count = 0, .limit = LVGL_DEVICES_MAX_PER_TYPE };
    device_for_each_of_type(&MOUSE_TYPE, &mouse_devices, lvgl_device_list_collect);
    for (size_t i = 0; i < mouse_devices.count; i++) {
        struct Device* kernel_mouse_device = mouse_devices.devices[i];
        lv_indev_t* lvgl_mouse_device;
        if (lvgl_mouse_add(kernel_mouse_device, lvgl_display, &lvgl_mouse_device) == ERROR_NONE) {
            LOG_I(TAG, "Bound %s to LVGL", kernel_mouse_device->name);
        } else {
            LOG_E(TAG, "Failed to bind %s to LVGL", kernel_mouse_device->name);
        }
    }

    devices_attached = true;
    device_listener_add(lvgl_device_listener, NULL);

    lvgl_unlock();
}

void lvgl_devices_detach() {
    lvgl_lock();

    devices_attached = false;
    device_listener_remove(lvgl_device_listener, NULL);

    lv_indev_t* indev = lv_indev_get_next(NULL);
    while (indev != NULL) {
        lv_indev_type_t type = lv_indev_get_type(indev);
        if (type == LV_INDEV_TYPE_POINTER || type == LV_INDEV_TYPE_ENCODER) {
            bool handled = false;
            void* driver_data = lv_indev_get_driver_data(indev);
            if (driver_data != nullptr) {
                LvglDeviceContext* context = static_cast<LvglDeviceContext*>(driver_data);
                if (context->device != nullptr) {
                    const DeviceType* device_type = device_get_type(context->device);
                    if (device_type == &POINTER_TYPE || device_type == &TOUCH_TYPE) {
                        lvgl_pointer_remove(indev);
                        handled = true;
                    } else if (device_type == &MOUSE_TYPE) {
                        lvgl_mouse_remove(indev);
                        handled = true;
                    } else {
                        LOG_E(TAG, "Unknown pointer device with possible memory leak of driver data");
                    }
                } else {
                    LOG_W(TAG, "Unknown pointer device (no data attached)");
                }
            }
            if (!handled) {
                lv_indev_delete(indev);
            }
        } else if (type == LV_INDEV_TYPE_KEYPAD) {
            lvgl_keyboard_remove(indev);
        } else {
            lv_indev_delete(indev);
        }
        // Always get the first item, because getting the next one doesn't work as the current pointer just became corrupt
        indev = lv_indev_get_next(NULL);
    }

    for (size_t i = 0; i < LVGL_DEVICES_MAX_PER_TYPE; i++) {
        IdleRepaintContext* ctx = &idle_repaint_contexts()[i];
        if (ctx->timer != nullptr) {
            lv_timer_delete(ctx->timer);
            *ctx = {};
        }
    }

    lv_disp_t* display = lv_disp_get_next(NULL);
    while (display != NULL) {
        lvgl_display_remove(display);
        display = lv_disp_get_next(NULL);
    }

    lvgl_theme_deinit();

    lvgl_unlock();
}

} // extern C
