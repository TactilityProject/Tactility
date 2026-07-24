#include <tactility/error.h>
#include <tactility/drivers/gps.h>
#include <tactility/gps_service.h>
#include <tactility/log.h>
#include <tactility/lvgl_module.h>
#include <tactility/module.h>

#include <Tactility/SystemEvents.h>
#include <Tactility/LogMessages.h>
#include <Tactility/settings/TrackballSettings.h>

#include <lilygo/drivers/trackball.h>
#include <lilygo/drivers/tdeck_power_on.h>

#include <tactility/delay.h>

#include <driver/gpio.h>

constexpr auto* TAG = "tdeck-plus";

extern "C" {

static tt::kernel::SystemEventSubscription tdeck_boot_splash_subscription = 0;

void init_gps_configuration() {
    bool has_configuration = false;
    gps_service_for_each_configuration(&has_configuration, [](const GpsConfiguration*, size_t, void* context) {
        *static_cast<bool*>(context) = true;
    });

    if (!has_configuration) {
        GpsConfiguration configuration = { .uart_name = "uart0", .baud_rate = 38400, .model = GpsModel::GPS_MODEL_UBLOX10 };
        if (gps_service_add_configuration(&configuration) == ERROR_NONE) {
            LOG_I(TAG, "Configured internal GPS");
        } else {
            LOG_E(TAG, "Failed to configure internal GPS");
        }
    }
}

void init_trackball() {
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
}

static error_t start() {
    LOG_I(TAG, LOG_MESSAGE_POWER_ON_START);

    if (!tdeck_power_on()) {
        LOG_E(TAG, LOG_MESSAGE_POWER_ON_FAILED);
        return ERROR_RESOURCE;
    }

    // Avoids crash when no SD card is inserted. It's unknown why, but likely is related to power draw.
    delay_millis(100);

    tdeck_boot_splash_subscription = tt::kernel::subscribeSystemEvent(tt::kernel::SystemEvent::BootSplash, [](tt::kernel::SystemEvent event) {
        init_gps_configuration();
        init_trackball();
    });

    return ERROR_NONE;
}

static error_t stop() {
    tt::kernel::unsubscribeSystemEvent(tdeck_boot_splash_subscription);
    tdeck_boot_splash_subscription = 0;
    return ERROR_NONE;
}

Module lilygo_tdeck_plus_module = {
    .name = "lilygo-tdeck-plus",
    .start = start,
    .stop = stop,
};

}
