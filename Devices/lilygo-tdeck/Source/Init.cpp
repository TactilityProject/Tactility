#include "PwmBacklight.h"
#include "devices/KeyboardBacklight.h"
#include "devices/TrackballDevice.h"

#include <Tactility/hal/gps/GpsConfiguration.h>
#include <Tactility/kernel/SystemEvents.h>
#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/service/gps/GpsService.h>

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

    // Avoids crash when no SD card is inserted. It's unknown why, but likely is related to power draw.
    vTaskDelay(100);

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
    });

    return true;
}
