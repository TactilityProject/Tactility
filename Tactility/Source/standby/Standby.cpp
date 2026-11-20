#define L10N_APP "standbysettings"
#include <Tactility/i18n/AppDomain.h>
#include <Tactility/standby/Standby.h>

#include <Tactility/app/boot/BootScreen.h>
#include <Tactility/lvgl/Lvgl.h>

#include <bluetooth/bluetooth_service.h>
#include <service/instance.h>
#include <service/manager.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/button.h>
#include <tactility/drivers/bluetooth.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/drivers/touch.h>
#include <tactility/drivers/wifi.h>
#include <tactility/delay.h>
#include <tactility/log.h>

#include <atomic>
#include <memory>


namespace tt::standby {

constexpr auto* TAG = "Standby";
constexpr auto* MESH_SERVICE_ID = "tactility.mesh";
constexpr auto* ENCODER_COMPATIBLE = "tactility,gpio-encoder";
constexpr auto* BUTTON_COMPATIBLE = "tactility,button-control";
constexpr TickType_t POLL_INTERVAL = pdMS_TO_TICKS(20);
constexpr TickType_t RELEASE_TIMEOUT = pdMS_TO_TICKS(1000);
/** How long nothing may be pressed before a press is considered finished */
constexpr TickType_t QUIET_TIME = pdMS_TO_TICKS(100);
constexpr TickType_t RADIO_TIMEOUT = pdMS_TO_TICKS(5000);
/** How long the text stays in one place on a display that remains on */
constexpr TickType_t MOVE_INTERVAL = pdMS_TO_TICKS(10000);

using settings::standby::WakeKind;

namespace {

struct RadioStates {
    bool wifi = false;
    bool bluetooth = false;
    bool mesh = false;
};

struct Awake {
    Device* device;
    WakeInput input;
    /** Set for keyboards, which can push their events without implementing read_key. It stays at one address while it is subscribed. */
    KeyboardEventSubscription* subscription = nullptr;
};

std::atomic<bool> standbyActive { false };

/** Buttons that were held when standby ended, until they are released. A few buttons exist on a device. */
constexpr size_t WAKE_GESTURE_CAPACITY = 4;
std::atomic<Device*> wakeGestures[WAKE_GESTURE_CAPACITY] = {};

WakeKind keyboardKind(Device* device) {
    auto* driver = device_get_driver(device);
    if (driver != nullptr) {
        if (driver_is_compatible(driver, ENCODER_COMPATIBLE)) {
            return WakeKind::Encoder;
        }
        if (driver_is_compatible(driver, BUTTON_COMPATIBLE)) {
            return WakeKind::Button;
        }
    }
    return WakeKind::Keyboard;
}

struct Collector {
    ::std::vector<WakeSource>* sources;
    WakeInput input;
};

void collect(const DeviceType* type, WakeInput input, ::std::vector<WakeSource>& sources) {
    Collector collector { &sources, input };
    device_for_each_of_type(type, &collector, [](Device* device, void* context) {
        auto* collector = static_cast<Collector*>(context);
        if (device_is_ready(device)) {
            WakeKind kind = WakeKind::Touch;
            if (collector->input == WakeInput::Button) {
                kind = WakeKind::Button;
            } else if (collector->input == WakeInput::Keyboard) {
                kind = keyboardKind(device);
            }
            collector->sources->push_back({ device->name, kind, collector->input });
        }
        return true;
    });
}

/** @return true when a key was pressed since the last call. Reads the driver too when it implements read_key. */
bool isKeyPressed(const Awake& keyboard) {
    if (keyboard.subscription == nullptr) {
        return false;
    }
    constexpr int maxReads = 16;
    bool pressed = false;
    KeyboardKeyData data;
    for (int i = 0; i < maxReads && keyboard_poll(keyboard.device, keyboard.subscription, &data) == ERROR_NONE; i++) {
        pressed |= data.pressed;
    }
    return pressed;
}

bool isTouched(Device* touch) {
    if (touch_read_data(touch, 0) != ERROR_NONE) {
        return false;
    }
    uint16_t x, y, strength;
    uint8_t pointCount = 0;
    return touch_get_touched_points(touch, &x, &y, &strength, &pointCount, 1);
}

bool isButtonPressed(Device* button) {
    bool pressed = false;
    return button_is_pressed(button, &pressed) == ERROR_NONE && pressed;
}

bool isActive(const Awake& awake) {
    switch (awake.input) {
        case WakeInput::Keyboard: return isKeyPressed(awake);
        case WakeInput::Touch: return isTouched(awake.device);
        case WakeInput::Button: return isButtonPressed(awake.device);
    }
    return false;
}

/** Waits until nothing was pressed for a while, so that the press that started or ended the standby isn't seen again */
void awaitRelease(const ::std::vector<Awake>& awakes) {
    const TickType_t start = xTaskGetTickCount();
    TickType_t last_active = start;
    while (xTaskGetTickCount() - start < RELEASE_TIMEOUT && xTaskGetTickCount() - last_active < QUIET_TIME) {
        for (const auto& awake : awakes) {
            if (isActive(awake)) {
                last_active = xTaskGetTickCount();
            }
        }
        vTaskDelay(POLL_INTERVAL);
    }
}

/** @param onInterval called every interval while waiting. A very long interval never calls it. */
template <typename OnInterval>
void awaitWake(const ::std::vector<Awake>& awakes, TickType_t interval, OnInterval onInterval) {
    TickType_t last_interval = xTaskGetTickCount();
    while (true) {
        for (const auto& awake : awakes) {
            if (isActive(awake)) {
                return;
            }
        }
        if (xTaskGetTickCount() - last_interval >= interval) {
            last_interval = xTaskGetTickCount();
            onInterval();
        }
        vTaskDelay(POLL_INTERVAL);
    }
}

template <typename Done>
void awaitUntil(Done done) {
    const TickType_t start = xTaskGetTickCount();
    while (!done() && xTaskGetTickCount() - start < RADIO_TIMEOUT) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/** Turns off the radios that aren't kept awake. The persisted choices stay as they are. */
RadioStates turnOffRadios(const settings::standby::StandbySettings& settings) {
    RadioStates was_on;

    Device* wifi = nullptr;
    if (device_get_first_by_type(&WIFI_TYPE, &wifi) == ERROR_NONE) {
        WifiRadioState state = WIFI_RADIO_STATE_OFF;
        wifi_get_radio_state(wifi, &state);
        if (!settings.keepWifi && (state == WIFI_RADIO_STATE_ON || state == WIFI_RADIO_STATE_ON_PENDING)) {
            was_on.wifi = true;
            wifi_set_radio_off(wifi);
            awaitUntil([wifi] {
                WifiRadioState current = WIFI_RADIO_STATE_ON;
                return wifi_get_radio_state(wifi, &current) != ERROR_NONE || current == WIFI_RADIO_STATE_OFF;
            });
        }
        device_put(wifi);
    }

    Device* bluetooth = nullptr;
    if (device_get_first_by_type(&BLUETOOTH_TYPE, &bluetooth) == ERROR_NONE) {
        if (!settings.keepBluetooth && bluetooth_is_radio_on_or_pending(bluetooth)) {
            was_on.bluetooth = true;
            bluetooth_service_set_radio_enabled(false);
            awaitUntil([bluetooth] {
                BtRadioState current = BT_RADIO_STATE_ON;
                return bluetooth_get_radio_state(bluetooth, &current) != ERROR_NONE || current == BT_RADIO_STATE_OFF;
            });
        }
        device_put(bluetooth);
    }

    if (!settings.keepMesh && service_manager_get_state(MESH_SERVICE_ID) == SERVICE_STATE_STARTED) {
        was_on.mesh = true;
        service_manager_stop(MESH_SERVICE_ID);
    }

    return was_on;
}

void resumeRadios(const RadioStates& was_on) {
    if (was_on.wifi) {
        Device* wifi = nullptr;
        if (device_get_first_by_type(&WIFI_TYPE, &wifi) == ERROR_NONE) {
            wifi_set_radio_on(wifi);
            device_put(wifi);
        }
    }
    if (was_on.bluetooth) {
        bluetooth_service_set_radio_enabled(true);
    }
    if (was_on.mesh) {
        service_manager_start(MESH_SERVICE_ID);
    }
}

struct Anchor {
    float x;
    float y;
};

/** The places of the text on a display that remains on, which differ from each other */
constexpr Anchor TEXT_ANCHORS[] = { { 0.5f, 0.5f }, { 0.2f, 0.25f }, { 0.8f, 0.75f }, { 0.2f, 0.75f }, { 0.8f, 0.25f } };
constexpr size_t TEXT_ANCHOR_COUNT = sizeof(TEXT_ANCHORS) / sizeof(TEXT_ANCHORS[0]);

/** Without LVGL, so the text goes through the display API */
void showStandbyScreen(Device* display, bool slowRefresh, size_t anchorIndex) {
    app::boot::BootScreen screen;
    if (screen.begin()) {
        const auto& anchor = TEXT_ANCHORS[anchorIndex % TEXT_ANCHOR_COUNT];
        screen.setAnchor(anchor.x, anchor.y);
        screen.show({}, { tr("Standby...") });
        screen.end();
    }
    if (!slowRefresh) {
        return;
    }
    // Clears ghosting, and drivers can finish drawing asynchronously after the refresh returns
    const error_t error = display_refresh(display);
    if (error != ERROR_NONE && error != ERROR_NOT_SUPPORTED) {
        LOG_W(TAG, "Display refresh failed: %s", error_to_string(error));
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
}

/** The backlight is dimmed to 10% of its range while the display stays on */
uint8_t getDimmedBrightness(Device* backlight) {
    const uint8_t min = backlight_get_min_brightness(backlight);
    const uint8_t max = backlight_get_max_brightness(backlight);
    return static_cast<uint8_t>(min + (max - min) / 10);
}

} // namespace

bool isActive() {
    return standbyActive.load(std::memory_order_acquire);
}

::std::vector<WakeSource> findWakeSources() {
    ::std::vector<WakeSource> sources;
    collect(&KEYBOARD_TYPE, WakeInput::Keyboard, sources);
    collect(&TOUCH_TYPE, WakeInput::Touch, sources);
    collect(&BUTTON_TYPE, WakeInput::Button, sources);
    return sources;
}

::std::vector<WakeSource> findEnabledWakeSources(const settings::standby::StandbySettings& settings) {
    ::std::vector<WakeSource> enabled;
    for (auto& source : findWakeSources()) {
        if (settings::standby::isWakeEnabled(settings, source.name, source.kind)) {
            enabled.push_back(source);
        }
    }
    return enabled;
}

bool isWakeGesture(Device* device) {
    for (auto& gesture : wakeGestures) {
        if (gesture.load(std::memory_order_acquire) == device) {
            return true;
        }
    }
    return false;
}

void endWakeGesture(Device* device) {
    for (auto& gesture : wakeGestures) {
        Device* expected = device;
        gesture.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }
}

bool enter(const settings::standby::StandbySettings& settings) {
    const auto sources = findEnabledWakeSources(settings);
    if (sources.empty()) {
        LOG_W(TAG, "No wake source is enabled");
        return false;
    }

    // The devices are acquired and checked before anything changes, as a device can be removed after it was discovered
    ::std::vector<Awake> awakes;
    for (const auto& source : sources) {
        Device* device = nullptr;
        if (device_get_by_name(source.name.c_str(), &device) == ERROR_NONE) {
            awakes.push_back({ device, source.input });
        }
    }
    // The subscriptions must stay at the same address while they are subscribed
    auto subscriptions = ::std::make_unique<KeyboardEventSubscription[]>(awakes.size());
    size_t usable_count = 0;
    for (size_t i = 0; i < awakes.size(); i++) {
        if (awakes[i].input == WakeInput::Keyboard) {
            if (keyboard_subscribe(awakes[i].device, &subscriptions[i]) == ERROR_NONE) {
                awakes[i].subscription = &subscriptions[i];
                usable_count++;
            }
        } else {
            usable_count++;
        }
    }
    auto release_awakes = [&] {
        for (const auto& awake : awakes) {
            if (awake.subscription != nullptr) {
                keyboard_unsubscribe(awake.device, awake.subscription);
            }
            device_put(awake.device);
        }
    };
    if (usable_count == 0) {
        LOG_W(TAG, "No wake source is available");
        release_awakes();
        return false;
    }

    bool expected_active = false;
    if (!standbyActive.compare_exchange_strong(expected_active, true, std::memory_order_acq_rel)) {
        LOG_W(TAG, "Standby is already active");
        release_awakes();
        return false;
    }

    // Touch devices that don't wake the device are put to sleep
    ::std::vector<Device*> standby_touch;
    for (const auto& source : findWakeSources()) {
        bool woken = false;
        for (const auto& enabled : sources) {
            woken |= enabled.name == source.name;
        }
        Device* device = nullptr;
        if (!woken && source.input == WakeInput::Touch && device_get_by_name(source.name.c_str(), &device) == ERROR_NONE) {
            if (touch_enter_sleep(device) == ERROR_NONE) {
                standby_touch.push_back(device);
            } else {
                device_put(device);
            }
        }
    }

    const RadioStates radios = turnOffRadios(settings);

    Device* display = nullptr;
    const bool has_display = device_get_first_by_type(&DISPLAY_TYPE, &display) == ERROR_NONE;
    Device* backlight = nullptr;
    uint8_t backlight_level = 0;
    if (has_display && display_get_backlight(display, &backlight) == ERROR_NONE) {
        backlight_get_brightness(backlight, &backlight_level);
    } else {
        backlight = nullptr;
    }

    awaitRelease(awakes);
    lvgl::stop();

    bool panel_off = false;
    bool moves_text = false;
    uint8_t standby_brightness = 0;
    if (has_display) {
        const bool slow_refresh = display_has_capability(display, DISPLAY_CAPABILITY_SLOW_REFRESH);
        if (slow_refresh || settings.keepDisplay) {
            showStandbyScreen(display, slow_refresh, 0);
        }
        // A lit display would burn in when the text stays in one place
        moves_text = settings.keepDisplay && !slow_refresh;
        if (settings.keepDisplay) {
            if (!slow_refresh && backlight != nullptr) {
                standby_brightness = getDimmedBrightness(backlight);
            }
        } else if (!slow_refresh && display_has_capability(display, DISPLAY_CAPABILITY_ON_OFF)) {
            panel_off = display_disp_on_off(display, false) == ERROR_NONE;
        }
    }
    if (backlight != nullptr) {
        backlight_set_brightness(backlight, standby_brightness);
    }

    // Drops what was queued while the display turned off
    awaitRelease(awakes);
    size_t anchor_index = 0;
    awaitWake(awakes, moves_text ? MOVE_INTERVAL : portMAX_DELAY, [&] {
        showStandbyScreen(display, false, ++anchor_index);
    });

    if (panel_off) {
        display_disp_on_off(display, true);
    }
    if (backlight != nullptr) {
        backlight_set_brightness(backlight, backlight_level);
        device_put(backlight);
    }
    if (has_display) {
        device_put(display);
    }
    for (auto* device : standby_touch) {
        touch_exit_sleep(device);
        device_put(device);
    }

    // A key that is still down would be seen as a fresh press by LVGL
    awaitRelease(awakes);
    // A button that is still held keeps its press and long press from being actions, until it is released
    size_t gesture_index = 0;
    for (const auto& awake : awakes) {
        if (awake.input == WakeInput::Button && gesture_index < WAKE_GESTURE_CAPACITY && isButtonPressed(awake.device)) {
            wakeGestures[gesture_index++].store(awake.device, std::memory_order_release);
        }
    }
    release_awakes();

    resumeRadios(radios);
    lvgl::start();
    standbyActive.store(false, std::memory_order_release);
    return true;
}

} // namespace tt::standby
