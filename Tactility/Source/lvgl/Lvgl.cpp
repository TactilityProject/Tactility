#include <Tactility/hal/Configuration.h>
#include <Tactility/hal/encoder/EncoderDevice.h>
#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/keyboard/KeyboardDevice.h>
#include <Tactility/hal/touch/TouchDevice.h>
#include <Tactility/lvgl/Keyboard.h>
#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/settings/DisplaySettings.h>
#include <Tactility/settings/TouchCalibrationSettings.h>

#include <tactility/device.h>
#include <tactility/drivers/backlight.h>
#include <tactility/log.h>
#include <tactility/lvgl_module.h>
#include <tactility/lvgl_pointer.h>
#include <tactility/module.h>

#include <lvgl.h>

namespace tt::lvgl {

constexpr auto* TAG = "LVGL";

bool isStarted() {
    return module_is_started(&lvgl_module);
}

void attachDevices() {
    LOG_I(TAG, "Adding devices");

    auto lock = getSyncLock()->asScopedLock();
    lock.lock();

    // Start displays (their related touch devices start automatically within)

    LOG_I(TAG, "Start displays");
    auto hal_displays= hal::findDevices<hal::display::DisplayDevice>(hal::Device::Type::Display);
    for (const auto& display: hal_displays) {
        if (display->supportsLvgl()) {
            if (display->startLvgl()) {
                LOG_I(TAG, "Started %s", display->getName().c_str());
            } else {
                LOG_E(TAG, "Start failed for %s", display->getName().c_str());
            }
        }
    }

    auto* primary_lvgl_display = lv_disp_get_default();
    if (primary_lvgl_display != nullptr) {
        LOG_I(TAG, "Set default display rotation");
        auto settings = settings::display::loadOrGetDefault();
        lv_display_rotation_t rotation = settings::display::toLvglDisplayRotation(settings.orientation);
        if (rotation != lv_display_get_rotation(primary_lvgl_display)) {
            lv_display_set_rotation(primary_lvgl_display, rotation);
        }
    }

    // Start touch

    if (primary_lvgl_display != nullptr) {
        LOG_I(TAG, "Start touch devices");
        auto touch_devices = hal::findDevices<hal::touch::TouchDevice>(hal::Device::Type::Touch);
        for (const auto& touch_device: touch_devices) {
            // Start any touch devices that haven't been started yet
            if (touch_device->supportsLvgl() && touch_device->getLvglIndev() == nullptr) {
                if (touch_device->startLvgl(primary_lvgl_display)) {
                    LOG_I(TAG, "Started %s", touch_device->getName().c_str());
                } else {
                    LOG_E(TAG, "Start failed for %s", touch_device->getName().c_str());
                }
            }
        }

        // Apply touch calibration (kernel POINTER_TYPE model only - see tactility/lvgl_pointer.h)
        LOG_I(TAG, "Apply touch calibration");
        auto touch_calibration_settings = settings::touch::loadOrGetDefault();
        if (touch_calibration_settings.enabled && settings::touch::isValid(touch_calibration_settings)) {
            auto* pointer_indev = lvgl_pointer_get_default();
            if (pointer_indev != nullptr) {
                struct LvglPointerCalibration calibration = {
                    .x_min = touch_calibration_settings.xMin,
                    .x_max = touch_calibration_settings.xMax,
                    .y_min = touch_calibration_settings.yMin,
                    .y_max = touch_calibration_settings.yMax,
                };
                if (lvgl_pointer_set_calibration(pointer_indev, &calibration) != ERROR_NONE) {
                    LOG_E(TAG, "Failed to apply saved touch calibration");
                }
            }
        }

        // Start keyboards
        LOG_I(TAG, "Start keyboards");
        auto keyboards = hal::findDevices<hal::keyboard::KeyboardDevice>(hal::Device::Type::Keyboard);
        for (const auto& keyboard: keyboards) {
            if (keyboard->isAttached()) {
                if (keyboard->startLvgl(primary_lvgl_display)) {
                    lv_indev_t* keyboard_indev = keyboard->getLvglIndev();
                    hardware_keyboard_set_indev(keyboard_indev);
                    LOG_I(TAG, "Started %s", keyboard->getName().c_str());
                } else {
                    LOG_E(TAG, "Start failed for %s", keyboard->getName().c_str());
                }
            }
        }

        // Start encoders
        LOG_I(TAG, "Start encoders");
        auto encoders = hal::findDevices<hal::encoder::EncoderDevice>(hal::Device::Type::Encoder);
        for (const auto& encoder: encoders) {
            if (encoder->startLvgl(primary_lvgl_display)) {
                LOG_I(TAG, "Started %s", encoder->getName().c_str());
            } else {
                LOG_E(TAG, "Start failed for %s", encoder->getName().c_str());
            }
        }
    }

    // Restart services

    // We search for the manifest first, because during the initial start() during boot
    // the service won't be registered yet.
    if (service::findManifestById("Gui") != nullptr) {
        if (service::getState("Gui") == SERVICE_STATE_STOPPED) {
            service::startService("Gui");
        } else {
            LOG_E(TAG, "Gui service is not in Stopped state");
        }
    }

    // We search for the manifest first, because during the initial start() during boot
    // the service won't be registered yet.
    if (service::findManifestById("Statusbar") != nullptr) {
        if (service::getState("Statusbar") == SERVICE_STATE_STOPPED) {
            service::startService("Statusbar");
        } else {
            LOG_E(TAG, "Statusbar service is not in Stopped state");
        }
    }
}

void detachDevices() {
    LOG_I(TAG, "Removing devices");

    auto lock = getSyncLock()->asScopedLock();
    lock.lock();

    // Stop services that highly depend on LVGL

    service::stopService("Statusbar");
    service::stopService("Gui");

    // Stop keyboards

    LOG_I(TAG, "Stopping keyboards");
    auto keyboards = hal::findDevices<hal::keyboard::KeyboardDevice>(hal::Device::Type::Keyboard);
    for (auto keyboard: keyboards) {
        if (keyboard->getLvglIndev() != nullptr) {
            keyboard->stopLvgl();
        }
    }

    // Stop touch

    LOG_I(TAG, "Stopping touch");
    // The display generally stops their own touch devices, but we'll clean up anything that didn't
    auto touch_devices = hal::findDevices<hal::touch::TouchDevice>(hal::Device::Type::Touch);
    for (auto touch_device: touch_devices) {
        if (touch_device->getLvglIndev() != nullptr) {
            touch_device->stopLvgl();
        }
    }

    // Stop encoders

    LOG_I(TAG, "Stopping encoders");
    // The display generally stops their own touch devices, but we'll clean up anything that didn't
    auto encoder_devices = hal::findDevices<hal::encoder::EncoderDevice>(hal::Device::Type::Encoder);
    for (auto encoder_device: encoder_devices) {
        if (encoder_device->getLvglIndev() != nullptr) {
            encoder_device->stopLvgl();
        }
    }
    // Stop displays (and their touch devices)

    LOG_I(TAG, "Stopping displays");
    auto displays = hal::findDevices<hal::display::DisplayDevice>(hal::Device::Type::Display);
    for (auto display: displays) {
        if (display->supportsLvgl() && display->getLvglDisplay() != nullptr && !display->stopLvgl()) {
            LOG_E(TAG, "Failed to detach display from LVGL");
        }
    }
}

void start() {
    check(module_start(&lvgl_module) == ERROR_NONE);
}

void stop() {
    check(module_stop(&lvgl_module) == ERROR_NONE);
}

} // namespace
