#include <Tactility/app/terminal/BreezyBox.h>

#include <tactility/log.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <tactility/device.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/drivers/pointer.h>
#include <tactility/module.h>
#include <lvgl/module.h>

constexpr auto* TAG = "BreezyBox";

// Built-in + one USB keyboard covers every board this app runs on today; a few spare slots in
// case more ever show up.
constexpr int MAX_KEYBOARDS = 4;

struct KeyboardCollection {
    Device* devices[MAX_KEYBOARDS] = {};
    int count = 0;
};

// Collects every active keyboard device - built-in and/or a USB HID keyboard child device (see
// esp32_usbhost_hid.cpp) both show up here identically, since both are plain KEYBOARD_TYPE
// devices. Pins each one (device_get()) so it can't be torn down (e.g. a USB keyboard unplugged)
// while the terminal is still using it - the caller releases them with device_put() when done.
static bool collectKeyboard(Device* device, void* context) {
    auto* collection = static_cast<KeyboardCollection*>(context);
    if (collection->count >= MAX_KEYBOARDS) {
        return false;
    }
    if (!device_is_ready(device) || device_get(device) != ERROR_NONE) {
        return true;
    }
    collection->devices[collection->count++] = device;
    return true;
}

// Shows a blocking error dialog and waits for it to close (so the user actually gets to read it)
// before the caller finishes this app - this app never creates a window of its own, so there's
// nothing else keeping it around for the dialog to be seen against.
static void showErrorAndWait(AppInstanceId appInstanceId, const char* message) {
    const char* argv[] = { "Error", message, "OK" };
    uint32_t dialogInstanceId = 0;
    app_start_for_result("tactility.alertdialog", 3, argv, appInstanceId, &dialogInstanceId);
    if (dialogInstanceId == 0) {
        return;
    }

    struct TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    struct AppEventSubscription sub {};
    app_event_subscribe(&sub, &event_group);

    while (true) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        bool done = false;
        struct AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_RESULT && event.result.launch_id == dialogInstanceId) {
                app_manager_stop(dialogInstanceId);
                done = true;
                break;
            }
        }
        if (done) break;
    }

    app_event_unsubscribe(&sub);
    task_event_group_destruct(&event_group);
}

namespace tt::app::terminal {

int main(int argc, char* argv[]) {
    AppInstanceId app_instance_id = app_scheduler_current_app_id();

    struct Device* display_device = nullptr;
    if (device_get_first_active_by_type(&DISPLAY_TYPE, &display_device) != ERROR_NONE) {
        LOG_E(TAG, "No display device found");
        showErrorAndWait(app_instance_id, "No display device was found.");
        return 0;
    }

    // A built-in keyboard and a USB one (see esp32_usbhost_hid.cpp) are both plain KEYBOARD_TYPE
    // devices, so one pass finds every keyboard this app can use. Only refuse to start when none
    // are present, since a terminal with no input is of no use.
    KeyboardCollection keyboards;
    device_for_each_of_type(&KEYBOARD_TYPE, &keyboards, collectKeyboard);
    if (keyboards.count == 0) {
        LOG_E(TAG, "No keyboard device found");
        device_put(display_device);
        showErrorAndWait(app_instance_id, "BreezyBox needs a keyboard.");
        return 0;
    }

    // Touch is only the exit gesture, so it is optional.
    struct Device* touch_device = nullptr;
    if (device_get_first_active_by_type(&POINTER_TYPE, &touch_device) != ERROR_NONE) {
        LOG_W(TAG, "No touch device found - exit gesture unavailable");
        touch_device = nullptr;
    }

    // Stop LVGL so this app owns the display and the keyboards' event queues. The keyboard Devices
    // are owned by the board's device module rather than by lvgl-module, so they stay started.
    module_stop(&lvgl_module);

    runTerminal(display_device, keyboards.devices, keyboards.count, touch_device);

    device_put(display_device);
    for (int i = 0; i < keyboards.count; i++) {
        device_put(keyboards.devices[i]);
    }
    if (touch_device != nullptr) {
        device_put(touch_device);
    }

    if (!module_is_started(&lvgl_module)) {
        LOG_I(TAG, "Restarting LVGL");
        module_start(&lvgl_module);
    }

    return 0;
}

extern const ::AppManifest manifest = {
    .id = "tactility.terminal",
    .name = "Terminal",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(main) },
    .flags = 0,
    .stack = {}
};

}
