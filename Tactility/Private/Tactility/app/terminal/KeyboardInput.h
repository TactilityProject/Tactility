#pragma once

#include <tactility/device.h>
#include <tactility/drivers/keyboard.h>

#include <tactility/freertos/freertos.h>

#include <vector>

/** Tracks every KEYBOARD_TYPE device, re-scanning at most once a second so a USB keyboard
 * plugged/unplugged after the terminal starts is picked up without restarting the app. */
class KeyboardInput {
public:
    using KeyHandler = bool (*)(unsigned int key, bool ctrl, bool alt);

    KeyboardInput();
    ~KeyboardInput();

    bool empty() const { return devices_.empty(); }

    /** @return true if onKey returned true for any key read */
    bool pump(KeyHandler onKey);

private:
    static constexpr uint32_t REFRESH_INTERVAL_MS = 1000;

    static bool collect(Device* device, void* context);

    void refresh();
    void rescan();

    std::vector<Device*> devices_;
    TickType_t lastRefresh_ = 0;
};
