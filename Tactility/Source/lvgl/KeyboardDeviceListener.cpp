#include <Tactility/lvgl/KeyboardDeviceListener.h>
#include <Tactility/Mutex.h>

#include <tactility/device.h>
#include <tactility/device_listener.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/log.h>

#include <lvgl/devices/keyboard.h>
#include <lvgl/lvgl.h>

#include <vector>

namespace tt::lvgl {

constexpr auto* TAG = "KeyboardDeviceListener";

namespace {

struct KeyboardBinding {
    Device* device;
    lv_indev_t* indev;
};

Mutex& bindingsMutex() {
    static Mutex mutex;
    return mutex;
}

std::vector<KeyboardBinding>& bindings() {
    static std::vector<KeyboardBinding> list;
    return list;
}

// Guards, under bindingsMutex, against onKeyboardDeviceStarted() registering a binding after
// stopKeyboardDeviceListener() has already drained bindings() and returned.
bool& listenerActive() {
    static bool active = false;
    return active;
}

void onKeyboardDeviceStarted(Device* device) {
    // Held for the whole add+register step so a same-device STOPPING can't observe the binding
    // as neither-registered-nor-added: onKeyboardDeviceStopped() and shutdown's drain both take
    // this same lock, so they see either the fully-registered binding or nothing at all.
    auto lock = bindingsMutex().asScopedLock();
    lock.lock();
    if (!listenerActive()) {
        return;
    }

    lv_indev_t* indev = nullptr;
    lvgl_lock();
    error_t error = lvgl_keyboard_add(device, lv_display_get_default(), &indev);
    lvgl_unlock();
    if (error != ERROR_NONE) {
        LOG_E(TAG, "failed to bind keyboard device %s to LVGL", device->name);
        return;
    }
    bindings().push_back({ device, indev });
}

void onKeyboardDeviceStopped(Device* device) {
    auto lock = bindingsMutex().asScopedLock();
    lock.lock();
    auto& list = bindings();
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->device == device) {
            list.erase(it);
            break;
        }
    }

    // Not every indev bound to a keyboard device goes through onKeyboardDeviceStarted() and
    // bindings() above. lvgl_devices_attach() also binds whatever KEYBOARD_TYPE devices are
    // already started at LVGL startup, before this listener is even registered (see
    // startKeyboardDeviceListener()'s call site). Look the indev up directly so that path's
    // devices still get detached before the kernel device is destructed, instead of leaving a
    // dangling indev that reads a freed device on the next LVGL tick.
    lvgl_lock();
    lv_indev_t* indev = lvgl_keyboard_find_by_device(device);
    if (indev != nullptr) {
        lvgl_keyboard_remove(indev);
    }
    lvgl_unlock();
}

void onDeviceEvent(Device* device, DeviceEvent event, void* context) {
    (void)context;
    if (device_get_type(device) != &KEYBOARD_TYPE) {
        return;
    }
    // Detach on STOPPING (before stop_device() frees the device's resources), not STOPPED (which
    // only fires after stop_device() already ran) - see DEVICE_EVENT_STOPPING's doc comment.
    if (event == DEVICE_EVENT_STARTED) {
        onKeyboardDeviceStarted(device);
    } else if (event == DEVICE_EVENT_STOPPING) {
        onKeyboardDeviceStopped(device);
    }
}

} // namespace

void startKeyboardDeviceListener() {
    auto lock = bindingsMutex().asScopedLock();
    lock.lock();
    listenerActive() = true;
    device_listener_add(onDeviceEvent, nullptr);
}

void stopKeyboardDeviceListener() {
    std::vector<KeyboardBinding> remaining;
    {
        // device_listener_remove() doesn't wait for an in-flight onDeviceEvent() to finish, so a
        // STARTED callback already past this point can still land after we drain below; it takes
        // this same lock and checks listenerActive() before registering, so it backs off instead
        // of adding a binding nothing will ever remove.
        auto lock = bindingsMutex().asScopedLock();
        lock.lock();
        listenerActive() = false;
        device_listener_remove(onDeviceEvent, nullptr);
        remaining = std::move(bindings());
        bindings().clear();
    }

    if (remaining.empty()) {
        return;
    }

    lvgl_lock();
    for (auto& binding : remaining) {
        lvgl_keyboard_remove(binding.indev);
    }
    lvgl_unlock();
}

} // namespace tt::lvgl
