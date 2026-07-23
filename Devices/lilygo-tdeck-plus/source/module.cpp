#include <tactility/module.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/lvgl_module.h>

#include <Tactility/SystemEvents.h>
#include <Tactility/LogMessages.h>
#include <Tactility/hal/Configuration.h>
#include <Tactility/hal/gps/GpsConfiguration.h>
#include <Tactility/kernel/Kernel.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/gps/GpsService.h>
#include <Tactility/settings/TrackballSettings.h>

#include <lilygo/drivers/trackball.h>
#include <lilygo/drivers/tdeck_power_on.h>

#include <driver/gpio.h>

constexpr auto* TAG = "tdeck-plus";

extern "C" {

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
        lvgl_lock();
        if (trackball::init() != nullptr) {
            trackball::setMode(tbSettings.trackballMode == tt::settings::trackball::TrackballMode::Pointer
                ? trackball::Mode::Pointer
                : trackball::Mode::Encoder);
            trackball::setEncoderSensitivity(tbSettings.encoderSensitivity);
            trackball::setPointerSensitivity(tbSettings.pointerSensitivity);
            trackball::setEnabled(tbSettings.trackballEnabled);
        }
        lvgl_unlock();
    });
}

static error_t start() {
    LOG_I(TAG, LOG_MESSAGE_POWER_ON_START);
    if (!tdeck_power_on()) {
        LOG_E(TAG, LOG_MESSAGE_POWER_ON_FAILED);
        return ERROR_RESOURCE;
    }

    // Avoids crash when no SD card is inserted. It's unknown why, but likely is related to power draw.
    tt::kernel::delayMillis(100);

    subscribe_events();

    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

Module lilygo_tdeck_plus_module = {
    .name = "lilygo-tdeck-plus",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
