#include "SimulatorProfile.h"
#include "drivers/sdl_display.h"

#include <app/manager.h>

#include <lvgl/devices/display.h>
#include <lvgl/theme.h>

#include <tactility/device.h>
#include <tactility/device_listener.h>
#include <tactility/driver.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <algorithm>
#include <cmath>
#include <cstring>

constexpr auto* TAG = "Simulator";

extern "C" {

extern Driver sdl_display_driver;
extern Driver sdl_backlight_driver;
extern Driver sdl_audio_driver;
extern Driver sdl_touch_driver;
extern Driver sdl_keyboard_driver;

static Driver* const simulator_drivers[] = {
    &sdl_display_driver,
    &sdl_backlight_driver,
    &sdl_audio_driver,
    &sdl_touch_driver,
    &sdl_keyboard_driver,
    nullptr
};

}

// These devices have no real bus to attach to (SDL has no notion of one), but every non-root
// device is still expected to have a parent (see Device::parent) - they're parented to root once
// it's available below.
static SdlDisplayConfig sdl_display_config = {};
static Device sdl_display_device {};
static Device sdl_backlight_device {};
static Device sdl_audio_device {};
static Device sdl_pointer_device {};
static Device sdl_keyboard_device {};

static bool construct_add_start(Device* device, Device* parent, const char* name, const void* config, const char* compatible) {
    device->address = 0;
    device->name = name;
    device->config = config;
    device->parent = nullptr;
    device->internal = nullptr;

    error_t error = device_construct(device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to construct %s: %s", name, error_to_string(error));
        return false;
    }

    device_set_parent(device, parent);

    Driver* driver = driver_find_compatible(compatible);
    if (driver == nullptr) {
        LOG_E(TAG, "No driver registered for %s", compatible);
        device_destruct(device);
        return false;
    }
    device_set_driver(device, driver);

    if (device_add(device) != ERROR_NONE) {
        LOG_E(TAG, "Failed to add %s", name);
        device_destruct(device);
        return false;
    }

    if (device_start(device) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start %s", name);
        device_remove(device);
        device_destruct(device);
        return false;
    }

    return true;
}

// Root is only constructed/added/started after all dts_modules (including this one) have already
// started (see kernel_init()), so it can't be looked up by name from this module's own start() -
// wait for its DEVICE_EVENT_STARTED instead, same as e.g. m5stack-tab5's display/keyboard detection.
static void on_root_started(Device* device, DeviceEvent event, void* context) {
    if (event != DEVICE_EVENT_STARTED || strcmp(device->name, "/") != 0) {
        return;
    }

    const auto& profile = simulator::getProfile(simulator::getActiveProfileIndex());
    const uint16_t short_side = std::min(profile.width, profile.height);
    const uint16_t long_side = std::max(profile.width, profile.height);
    sdl_display_config = {
        profile.portrait ? short_side : long_side,
        profile.portrait ? long_side : short_side,
        profile.mono
    };

    construct_add_start(&sdl_display_device, device, "display0", &sdl_display_config, "tactility,sdl-display");
    // A monochrome display has no backlight to dim
    if (!profile.mono) {
        construct_add_start(&sdl_backlight_device, &sdl_display_device, "backlight0", nullptr, "tactility,sdl-backlight");
    }
    construct_add_start(&sdl_audio_device, device, "audio0", nullptr, "tactility,sdl-audio");
    if (profile.touch) {
        construct_add_start(&sdl_pointer_device, device, "touch0", nullptr, "tactility,sdl-touch");
    }
    if (profile.keyboard) {
        construct_add_start(&sdl_keyboard_device, device, "keyboard0", nullptr, "tactility,sdl-keyboard");
    }
}

extern "C" {

static error_t start() {
    simulator::initActiveProfile();
    const auto& profile = simulator::getProfile(simulator::getActiveProfileIndex());
    lvgl_theme_set_default_density(profile.uiSpacing, profile.uiSize);
    // The display has the resolution and the dpi of the device, so the layout is the same as on the device
    lvgl_display_set_default_dpi(profile.dpi);
    lvgl_theme_set_default_font_size(profile.fontSize);
    lvgl_theme_set_launcher_scales(
        static_cast<uint16_t>(std::lround(profile.launcherIconSizeScale * 100)),
        static_cast<uint16_t>(std::lround(profile.launcherIconSpacingScale * 100))
    );
    error_t error = app_manager_add(&simulator::profileAppManifest);
    if (error != ERROR_NONE) {
        return error;
    }
    device_listener_add(on_root_started, nullptr);
    return ERROR_NONE;
}

static error_t stop() {
    device_listener_remove(on_root_started, nullptr);
    app_manager_remove(simulator::profileAppManifest.id);
    return ERROR_NONE;
}

Module simulator_module = {
    .name = "simulator",
    .start = start,
    .stop = stop,
    .drivers = simulator_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
