#include <tactility/module.h>

#include <tactility/driver.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <Tactility/SystemEvents.h>
#include <Tactility/LogMessages.h>
#include <Tactility/hal/gps/GpsConfiguration.h>
#include <Tactility/kernel/Kernel.h>
#include <Tactility/service/gps/GpsService.h>

#include "devices/TrackballDevice.h"

#include <driver/gpio.h>

constexpr auto* TAG = "T-Deck";

constexpr auto TDECK_POWERON_GPIO = GPIO_NUM_10;

extern "C" {

extern Driver tdeck_keyboard_driver;
extern Driver tdeck_keyboard_backlight_driver;

static bool power_on() {
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
    tt::kernel::delayMillis(100);

    return true;
}

void subscribe_events() {
    tt::kernel::subscribeSystemEvent(tt::kernel::SystemEvent::BootSplash, [](tt::kernel::SystemEvent event) {
        auto gps_service = tt::service::gps::findGpsService();
        if (gps_service != nullptr) {
            std::vector<tt::hal::gps::GpsConfiguration> gps_configurations;
            gps_service->getGpsConfigurations(gps_configurations);
            if (gps_configurations.empty()) {
                if (gps_service->addGpsConfiguration(tt::hal::gps::GpsConfiguration {.uartName = "uart0", .baudRate = 38400, .model = tt::hal::gps::GpsModel::UBLOX10})) {
                    LOG_I(TAG, "Configured internal GPS");
                } else {
                    LOG_E(TAG, "Failed to configure internal GPS");
                }
            }
        }
    });

    tt::kernel::subscribeSystemEvent(tt::kernel::SystemEvent::BootSplash, [](tt::kernel::SystemEvent event) {
        auto trackball = tt::hal::findDevice("Trackball");
        if (trackball != nullptr) {
            LOG_I(TAG, "%s starting", trackball->getName().c_str());
            auto tbDevice = std::static_pointer_cast<TrackballDevice>(trackball);
            if (tbDevice->start()) {
                LOG_I(TAG, "%s started", trackball->getName().c_str());
            } else {
                LOG_E(TAG, "%s start failed", trackball->getName().c_str());
            }
        }
    });
}

static error_t start() {
    LOG_I(TAG, LOG_MESSAGE_POWER_ON_START);
    if (!power_on()) {
        LOG_E(TAG, LOG_MESSAGE_POWER_ON_FAILED);
        return ERROR_RESOURCE;
    }

    if (driver_construct_add(&tdeck_keyboard_driver) != ERROR_NONE) {
        LOG_E(TAG, "Failed to register keyboard driver");
        return ERROR_RESOURCE;
    }

    if (driver_construct_add(&tdeck_keyboard_backlight_driver) != ERROR_NONE) {
        LOG_E(TAG, "Failed to register keyboard backlight driver");
        return ERROR_RESOURCE;
    }

    subscribe_events();

    return ERROR_NONE;
}

static error_t stop() {
    // We never stop this module, but keep driver registration symmetric in case that changes.
    if (driver_remove_destruct(&tdeck_keyboard_driver) != ERROR_NONE) {
        LOG_E(TAG, "Failed to unregister keyboard driver");
        return ERROR_RESOURCE;
    }
    if (driver_remove_destruct(&tdeck_keyboard_backlight_driver) != ERROR_NONE) {
        LOG_E(TAG, "Failed to unregister keyboard backlight driver");
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

struct Module lilygo_tdeck_module = {
    .name = "lilygo-tdeck",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
