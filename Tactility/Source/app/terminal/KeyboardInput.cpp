#include <Tactility/app/terminal/KeyboardInput.h>

#include <lvgl/devices/keyboard.h>
#include <lvgl/lvgl.h>

#include <algorithm>

constexpr auto* TAG = "terminal-keyb";

KeyboardInput::KeyboardInput() {
    rescan();
}

KeyboardInput::~KeyboardInput() {
    awaitKeyRelease();
    setExclusive(false);
    for (Device* device : devices_) {
        device_put(device);
    }
}

/** A key that is still down would otherwise be seen as a fresh press by the LVGL indevs that are enabled again */
void KeyboardInput::awaitKeyRelease() {
    constexpr TickType_t timeout = pdMS_TO_TICKS(1000);
    const TickType_t start = xTaskGetTickCount();
    bool anyPressed = true;
    while (anyPressed && xTaskGetTickCount() - start < timeout) {
        anyPressed = false;
        for (Device* keyboard : devices_) {
            KeyboardKeyData data;
            if (device_is_ready(keyboard) && keyboard_read_key(keyboard, &data) == ERROR_NONE && data.pressed) {
                anyPressed = true;
            }
        }
        if (anyPressed) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

bool KeyboardInput::pump(KeyHandler onKey) {
    refresh();

    bool handled = false;
    KeyboardKeyData data;
    for (Device* keyboard : devices_) {
        if (!device_is_ready(keyboard)) {
            continue;
        }
        while (true) {
            if (keyboard_read_key(keyboard, &data) != ERROR_NONE || !data.pressed) {
                heldKey_ = 0;
                break;
            }
            if (data.key != heldKey_) {
                heldKey_ = data.key;
                if (onKey(data.key, data.ctrl, data.alt)) {
                    handled = true;
                }
            }
            if (!data.continue_reading) {
                break;
            }
        }
    }

    return handled;
}

void KeyboardInput::setExclusive(bool exclusive) {
    if (exclusive == exclusive_) {
        return;
    }
    exclusive_ = exclusive;
    applyExclusive();
}

void KeyboardInput::applyExclusive() {
    if (!lvgl_is_running()) {
        return;
    }
    lvgl_lock();
    for (Device* device : devices_) {
        lv_indev_t* indev = lvgl_keyboard_find_by_device(device);
        if (indev != nullptr) {
            lv_indev_enable(indev, !exclusive_);
        }
    }
    lvgl_unlock();
}

bool KeyboardInput::collect(Device* device, void* context) {
    static_cast<std::vector<Device*>*>(context)->push_back(device);
    return true;
}

void KeyboardInput::refresh() {
    if (xTaskGetTickCount() - lastRefresh_ < pdMS_TO_TICKS(REFRESH_INTERVAL_MS)) {
        return;
    }
    rescan();
}

void KeyboardInput::rescan() {
    lastRefresh_ = xTaskGetTickCount();

    std::vector<Device*> found;
    device_for_each_of_type(&KEYBOARD_TYPE, &found, collect);

    for (Device* device : found) {
        if (
            device_is_ready(device) &&
            std::find(devices_.begin(), devices_.end(), device) == devices_.end() &&
            device_get(device) == ERROR_NONE
        ) {
            devices_.push_back(device);
            LOG_I(TAG, "Found keyboard: %s", device->name);
        }
    }
    for (auto it = devices_.begin(); it != devices_.end();) {
        if (std::find(found.begin(), found.end(), *it) == found.end()) {
            device_put(*it);
            it = devices_.erase(it);
            LOG_I(TAG, "Removed keyboard");
        } else {
            ++it;
        }
    }

    // A keyboard connected since the last scan gets an enabled LVGL indev of its own.
    if (exclusive_) {
        applyExclusive();
    }
}
