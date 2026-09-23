#include <Tactility/app/terminal/KeyboardInput.h>

#include <algorithm>

constexpr auto* TAG = "terminal-keyb";

KeyboardInput::KeyboardInput() {
    rescan();
}

KeyboardInput::~KeyboardInput() {
    for (Device* device : devices_) {
        device_put(device);
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
                break;
            }
            if (onKey(data.key, data.ctrl, data.alt)) {
                handled = true;
            }
            if (!data.continue_reading) {
                break;
            }
        }
    }

    return handled;
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
}
