#include <tactility/module.h>

#include <tactility/driver.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <Tactility/SystemEvents.h>
#include <Tactility/LogMessages.h>
#include <Tactility/hal/Configuration.h>
#include <Tactility/hal/gps/GpsConfiguration.h>
#include <Tactility/kernel/Kernel.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/gps/GpsService.h>
#include <Tactility/settings/TrackballSettings.h>

#include <drivers/trackball.h>

#include <driver/gpio.h>

constexpr auto* TAG = "T-Deck";

constexpr auto TDECK_POWERON_GPIO = GPIO_NUM_10;

// Legacy placeholder (required until legacy HAL is cleaned up everywhere)
extern const tt::hal::Configuration hardwareConfiguration = {};

extern "C" {

extern Driver tdeck_keyboard_driver;
extern Driver tdeck_keyboard_backlight_driver;
extern Driver tdeck_trackball_driver;

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

    // The kernel trackball device is already started by kernel_init(); this just registers it as an
    // LVGL input device and applies persisted settings, both of which require LVGL to be up first.
    tt::kernel::subscribeSystemEvent(tt::kernel::SystemEvent::BootSplash, [](tt::kernel::SystemEvent event) {
        auto tbSettings = tt::settings::trackball::loadOrGetDefault();
        if (tt::lvgl::lock(100)) {
            if (trackball::init() != nullptr) {
                trackball::setMode(tbSettings.trackballMode == tt::settings::trackball::TrackballMode::Pointer
                    ? trackball::Mode::Pointer
                    : trackball::Mode::Encoder);
                trackball::setEncoderSensitivity(tbSettings.encoderSensitivity);
                trackball::setPointerSensitivity(tbSettings.pointerSensitivity);
                trackball::setEnabled(tbSettings.trackballEnabled);
            }
            tt::lvgl::unlock();
        } else {
            LOG_W(TAG, "Failed to acquire LVGL lock for trackball settings");
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

    if (driver_construct_add(&tdeck_trackball_driver) != ERROR_NONE) {
        LOG_E(TAG, "Failed to register trackball driver");
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
    if (driver_remove_destruct(&tdeck_trackball_driver) != ERROR_NONE) {
        LOG_E(TAG, "Failed to unregister trackball driver");
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
