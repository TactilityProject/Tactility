#include "tab5_keyboard_lvgl_rotation.h"

#include <tactility/device.h>
#include <tactility/device_listener.h>

#include <lvgl/lvgl.h>
#include <lvgl.h>

#include <atomic>

static lv_display_rotation_t saved_rotation = LV_DISPLAY_ROTATION_0;
static bool rotation_override_active = false;
// device_listener_remove() doesn't wait for an in-flight on_device_event() callback to finish -
// a DEVICE_EVENT_STARTED already snapshotted by device_listener_notify() can still fire after
// stop() has run. This flag makes such a late callback a no-op instead of re-applying the
// override after shutdown.
static std::atomic<bool> listener_active { false };

static void apply_rotation(bool attached) {
    lvgl_lock();

    auto* display = lv_display_get_default();
    if (display == nullptr) {
        lvgl_unlock();
        return;
    }

    if (attached) {
        if (lv_display_get_rotation(display) != LV_DISPLAY_ROTATION_90) {
            saved_rotation = lv_display_get_rotation(display);
            rotation_override_active = true;
            lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);
        }
    } else if (rotation_override_active) {
        // Ownership of the rotation ends here regardless - only restore it if the display is
        // still at what we set it to (the user may have changed it manually while attached).
        if (lv_display_get_rotation(display) == LV_DISPLAY_ROTATION_90) {
            lv_display_set_rotation(display, saved_rotation);
        }
        rotation_override_active = false;
    }

    lvgl_unlock();
}

static void on_device_event(Device* device, DeviceEvent event, void*) {
    if (!listener_active.load(std::memory_order_acquire) || !device_is_compatible(device, "m5stack,tab5-keyboard")) {
        return;
    }
    if (event == DEVICE_EVENT_STARTED) {
        apply_rotation(true);
    } else if (event == DEVICE_EVENT_STOPPED) {
        apply_rotation(false);
    }
}

void tab5_keyboard_lvgl_rotation_start() {
    rotation_override_active = false;
    listener_active.store(true, std::memory_order_release);
    device_listener_add(on_device_event, nullptr);
}

void tab5_keyboard_lvgl_rotation_stop() {
    // Must run before apply_rotation()/device_listener_remove(): a DEVICE_EVENT_STARTED already
    // snapshotted by device_listener_notify() before this call can still invoke on_device_event()
    // after device_listener_remove() returns - this flag makes that late callback a no-op.
    listener_active.store(false, std::memory_order_release);
    apply_rotation(false);
    device_listener_remove(on_device_event, nullptr);
}
