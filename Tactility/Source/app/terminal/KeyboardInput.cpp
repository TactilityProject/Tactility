#include <Tactility/app/terminal/KeyboardInput.h>

#include <lvgl/devices/keyboard.h>
#include <lvgl/lvgl.h>

#include <algorithm>
#include <vector>

constexpr auto* TAG = "terminal-keyb";

KeyboardInput::KeyboardInput() {
    rescan();
}

bool KeyboardInput::Keyboard::isHeld(uint32_t key) const {
    return std::find(held, held + heldCount, key) != held + heldCount;
}

void KeyboardInput::Keyboard::addHeld(uint32_t key) {
    if (heldCount < HELD_CAPACITY) {
        held[heldCount++] = key;
    }
}

void KeyboardInput::Keyboard::removeHeld(uint32_t key) {
    for (size_t i = 0; i < heldCount; i++) {
        if (held[i] == key) {
            held[i] = held[--heldCount];
            return;
        }
    }
}

KeyboardInput::~KeyboardInput() {
    if (exclusive_) {
        awaitKeyRelease();
        setExclusive(false);
    }
    for (const Keyboard& keyboard : devices_) {
        device_put(keyboard.device);
    }
}

/**
 * Drains the queued events of a keyboard, which ends in an idle read when nothing is down,
 * or in a pressed read when a key is still down.
 * @return true when a key is still down
 */
static bool isAnyKeyDown(Device* keyboard) {
    constexpr int maxReads = 64;
    KeyboardKeyData data;
    for (int i = 0; i < maxReads; i++) {
        if (keyboard_read_key(keyboard, &data) != ERROR_NONE) {
            return false;
        }
        if (data.pressed && !data.continue_reading) {
            return true;
        }
        if (!data.pressed && data.key == 0 && !data.continue_reading) {
            return false;
        }
    }
    return false;
}

/** A key that is still down would otherwise be seen as a fresh press by the LVGL indevs that are enabled again */
void KeyboardInput::awaitKeyRelease() {
    constexpr TickType_t timeout = pdMS_TO_TICKS(1000);
    const TickType_t start = xTaskGetTickCount();
    bool anyPressed = true;
    while (anyPressed && xTaskGetTickCount() - start < timeout) {
        anyPressed = false;
        for (const Keyboard& entry : devices_) {
            if (device_is_ready(entry.device) && isAnyKeyDown(entry.device)) {
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
    for (Keyboard& keyboard : devices_) {
        if (!device_is_ready(keyboard.device)) {
            continue;
        }
        while (true) {
            if (keyboard_read_key(keyboard.device, &data) != ERROR_NONE) {
                keyboard.clearHeld();
                break;
            }
            if (!data.pressed) {
                if (data.key == 0) {
                    // Nothing is down
                    keyboard.clearHeld();
                    break;
                }
                keyboard.removeHeld(data.key);
            } else if (!keyboard.isHeld(data.key)) {
                keyboard.addHeld(data.key);
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
    if (!exclusive) {
        // pump() doesn't run while hidden, so a release during that time is never seen
        for (Keyboard& keyboard : devices_) {
            keyboard.clearHeld();
        }
    }
    applyExclusive();
}

void KeyboardInput::applyExclusive() {
    if (!lvgl_is_running()) {
        return;
    }
    lvgl_lock();
    for (const Keyboard& keyboard : devices_) {
        lv_indev_t* indev = lvgl_keyboard_find_by_device(keyboard.device);
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
            std::find_if(devices_.begin(), devices_.end(), [device](const Keyboard& keyboard) { return keyboard.device == device; }) == devices_.end() &&
            device_get(device) == ERROR_NONE
        ) {
            devices_.push_back({ device });
            LOG_I(TAG, "Found keyboard: %s", device->name);
        }
    }
    for (auto it = devices_.begin(); it != devices_.end();) {
        if (std::find(found.begin(), found.end(), it->device) == found.end()) {
            device_put(it->device);
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
