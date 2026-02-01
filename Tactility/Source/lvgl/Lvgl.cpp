#include <Tactility/hal/Configuration.h>
#include <Tactility/hal/encoder/EncoderDevice.h>
#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/keyboard/KeyboardDevice.h>
#include <Tactility/hal/touch/TouchDevice.h>
#include <Tactility/Logger.h>
#include <Tactility/lvgl/Keyboard.h>
#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/settings/DisplaySettings.h>

#include <tactility/lvgl_module.h>
#include <tactility/module.h>

#include <lvgl.h>

namespace tt::lvgl {

static const auto LOGGER = Logger("LVGL");

bool isStarted() {
    return module_is_started(&lvgl_module);
}

static void on_start() {
    LOGGER.info("Adding devices");

    auto lock = getSyncLock()->asScopedLock();
    lock.lock();

    // Start displays (their related touch devices start automatically within)

    LOGGER.info("Start displays");
    auto displays = hal::findDevices<hal::display::DisplayDevice>(hal::Device::Type::Display);
    for (const auto& display: displays) {
        if (display->supportsLvgl()) {
            if (display->startLvgl()) {
                LOGGER.info("Started {}", display->getName());
                auto lvgl_display = display->getLvglDisplay();
                assert(lvgl_display != nullptr);
                auto settings = settings::display::loadOrGetDefault();
                lv_display_rotation_t rotation = settings::display::toLvglDisplayRotation(settings.orientation);
                if (rotation != lv_display_get_rotation(lvgl_display)) {
                    lv_display_set_rotation(lvgl_display, rotation);
                }
            } else {
                LOGGER.error("Start failed for {}", display->getName());
            }
        }
    }

    // Start touch

    // TODO: Consider implementing support for multiple displays
    auto primary_display = !displays.empty() ? displays[0] : nullptr;

    // Start display-related peripherals
    if (primary_display != nullptr) {
        LOGGER.info("Start touch devices");
        auto touch_devices = hal::findDevices<hal::touch::TouchDevice>(hal::Device::Type::Touch);
        for (const auto& touch_device: touch_devices) {
            // Start any touch devices that haven't been started yet
            if (touch_device->supportsLvgl() && touch_device->getLvglIndev() == nullptr) {
                if (touch_device->startLvgl(primary_display->getLvglDisplay())) {
                    LOGGER.info("Started {}", touch_device->getName());
                } else {
                    LOGGER.error("Start failed for {}", touch_device->getName());
                }
            }
        }

        // Start keyboards
        LOGGER.info("Start keyboards");
        auto keyboards = hal::findDevices<hal::keyboard::KeyboardDevice>(hal::Device::Type::Keyboard);
        for (const auto& keyboard: keyboards) {
            if (keyboard->isAttached()) {
                if (keyboard->startLvgl(primary_display->getLvglDisplay())) {
                    lv_indev_t* keyboard_indev = keyboard->getLvglIndev();
                    hardware_keyboard_set_indev(keyboard_indev);
                    LOGGER.info("Started {}", keyboard->getName());
                } else {
                    LOGGER.error("Start failed for {}", keyboard->getName());
                }
            }
        }

        // Start encoders
        LOGGER.info("Start encoders");
        auto encoders = hal::findDevices<hal::encoder::EncoderDevice>(hal::Device::Type::Encoder);
        for (const auto& encoder: encoders) {
            if (encoder->startLvgl(primary_display->getLvglDisplay())) {
                LOGGER.info("Started {}", encoder->getName());
            } else {
                LOGGER.error("Start failed for {}", encoder->getName());
            }
        }
    }

    // Restart services

    // We search for the manifest first, because during the initial start() during boot
    // the service won't be registered yet.
    if (service::findManifestById("Gui") != nullptr) {
        if (service::getState("Gui") == service::State::Stopped) {
            service::startService("Gui");
        } else {
            LOGGER.error("Gui service is not in Stopped state");
        }
    }

    // We search for the manifest first, because during the initial start() during boot
    // the service won't be registered yet.
    if (service::findManifestById("Statusbar") != nullptr) {
        if (service::getState("Statusbar") == service::State::Stopped) {
            service::startService("Statusbar");
        } else {
            LOGGER.error("Statusbar service is not in Stopped state");
        }
    }
}

static void on_stop() {
    LOGGER.info("Removing devices");

    auto lock = getSyncLock()->asScopedLock();
    lock.lock();

    // Stop services that highly depend on LVGL

    service::stopService("Statusbar");
    service::stopService("Gui");

    // Stop keyboards

    LOGGER.info("Stopping keyboards");
    auto keyboards = hal::findDevices<hal::keyboard::KeyboardDevice>(hal::Device::Type::Keyboard);
    for (auto keyboard: keyboards) {
        if (keyboard->getLvglIndev() != nullptr) {
            keyboard->stopLvgl();
        }
    }

    // Stop touch

    LOGGER.info("Stopping touch");
    // The display generally stops their own touch devices, but we'll clean up anything that didn't
    auto touch_devices = hal::findDevices<hal::touch::TouchDevice>(hal::Device::Type::Touch);
    for (auto touch_device: touch_devices) {
        if (touch_device->getLvglIndev() != nullptr) {
            touch_device->stopLvgl();
        }
    }

    // Stop encoders

    LOGGER.info("Stopping encoders");
    // The display generally stops their own touch devices, but we'll clean up anything that didn't
    auto encoder_devices = hal::findDevices<hal::encoder::EncoderDevice>(hal::Device::Type::Encoder);
    for (auto encoder_device: encoder_devices) {
        if (encoder_device->getLvglIndev() != nullptr) {
            encoder_device->stopLvgl();
        }
    }
    // Stop displays (and their touch devices)

    LOGGER.info("Stopping displays");
    auto displays = hal::findDevices<hal::display::DisplayDevice>(hal::Device::Type::Display);
    for (auto display: displays) {
        if (display->supportsLvgl() && display->getLvglDisplay() != nullptr && !display->stopLvgl()) {
            LOGGER.error("Failed to detach display from LVGL");
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
