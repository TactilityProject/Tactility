#include "PwmBacklight.h"
#include "devices/KeyboardBacklight.h"
#include "devices/TrackballDevice.h"

#include <Tactility/hal/gps/GpsConfiguration.h>
#include <Tactility/kernel/SystemEvents.h>
#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/gps/GpsService.h>
#include <Tactility/settings/KeyboardSettings.h>
#include <Tactility/settings/TrackballSettings.h>
#include <Trackball/Trackball.h>

#include <KeyboardBacklight/KeyboardBacklight.h>

static const auto LOGGER = tt::Logger("T-Deck");

// Power on
constexpr auto TDECK_POWERON_GPIO = GPIO_NUM_10;

static bool powerOn() {
    gpio_config_t device_power_signal_config = {
        .pin_bit_mask = BIT64(TDECK_POWERON_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (gpio_config(&device_power_signal_config) != ESP_OK) {
        return false;
    }

    if (gpio_set_level(TDECK_POWERON_GPIO, 1) != ESP_OK) {
        return false;
    }

    return true;
}

bool initBoot() {
    LOGGER.info(LOG_MESSAGE_POWER_ON_START);
    if (!powerOn()) {
        LOGGER.error(LOG_MESSAGE_POWER_ON_FAILED);
        return false;
    }

    /* 32 Khz and higher gives an issue where the screen starts dimming again above 80% brightness
     * when moving the brightness slider rapidly from a lower setting to 100%.
     * This is not a slider bug (data was debug-traced) */
    if (!driver::pwmbacklight::init(GPIO_NUM_42, 30000)) {
        LOGGER.error("Backlight init failed");
        return false;
    }

    tt::kernel::subscribeSystemEvent(tt::kernel::SystemEvent::BootSplash, [](tt::kernel::SystemEvent event) {
        auto gps_service = tt::service::gps::findGpsService();
        if (gps_service != nullptr) {
            std::vector<tt::hal::gps::GpsConfiguration> gps_configurations;
            gps_service->getGpsConfigurations(gps_configurations);
            if (gps_configurations.empty()) {
                if (gps_service->addGpsConfiguration(tt::hal::gps::GpsConfiguration {.uartName = "Grove", .baudRate = 38400, .model = tt::hal::gps::GpsModel::UBLOX10})) {
                    LOGGER.info("Configured internal GPS");
                } else {
                    LOGGER.error("Failed to configure internal GPS");
                }
            }
        }
    });

    tt::kernel::subscribeSystemEvent(tt::kernel::SystemEvent::BootSplash, [](tt::kernel::SystemEvent event) {
        auto kbBacklight = tt::hal::findDevice("Keyboard Backlight");
        if (kbBacklight != nullptr) {
            LOGGER.info("{} starting", kbBacklight->getName());
            auto kbDevice = std::static_pointer_cast<KeyboardBacklightDevice>(kbBacklight);
            if (kbDevice->start()) {
                LOGGER.info("{} started", kbBacklight->getName());
            } else {
                LOGGER.error("{} start failed", kbBacklight->getName());
            }
        }

        auto trackball = tt::hal::findDevice("Trackball");
        if (trackball != nullptr) {
            LOGGER.info("{} starting", trackball->getName());
            auto tbDevice = std::static_pointer_cast<TrackballDevice>(trackball);
            if (tbDevice->start()) {
                LOGGER.info("{} started", trackball->getName());
            } else {
                LOGGER.error("{} start failed", trackball->getName());
            }
        }

        // Backlight doesn't seem to turn on until toggled on and off from keyboard settings...
        // Or let the display and backlight sleep then wake it up.
        // Then it works fine...until reboot, then you need to toggle again.
        // The current keyboard firmware sets backlight duty to 0 on boot.
        // https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/firmware/T-Keyboard_Keyboard_ESP32C3_250620.bin
        // https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino#L25
        // https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino#L217
        auto kbSettings = tt::settings::keyboard::loadOrGetDefault();
        bool result = keyboardbacklight::setBrightness(kbSettings.backlightEnabled ? kbSettings.backlightBrightness : 0);
        if (!result) {
            LOGGER.warn("Failed to set keyboard backlight brightness");
        }

        // Apply trackball settings (requires LVGL lock for cursor manipulation)
        auto tbSettings = tt::settings::trackball::loadOrGetDefault();
        if (tt::lvgl::lock(100)) {
            trackball::setMode(tbSettings.trackballMode == tt::settings::trackball::TrackballMode::Pointer
                ? trackball::Mode::Pointer
                : trackball::Mode::Encoder);
            trackball::setEncoderSensitivity(tbSettings.encoderSensitivity);
            trackball::setPointerSensitivity(tbSettings.pointerSensitivity);
            trackball::setEnabled(tbSettings.trackballEnabled);
            tt::lvgl::unlock();
        } else {
            LOGGER.warn("Failed to acquire LVGL lock for trackball settings");
        }
    });

    return true;
}
