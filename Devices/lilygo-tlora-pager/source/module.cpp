#include <tactility/check.h>
#include <tactility/driver.h>
#include <tactility/module.h>

#include <Tactility/LogMessages.h>
#include <Tactility/SystemEvents.h>
#include <Tactility/hal/Configuration.h>
#include <Tactility/hal/gps/GpsConfiguration.h>
#include <Tactility/kernel/Kernel.h>
#include <Tactility/service/gps/GpsService.h>
#include <tactility/log.h>
#include <tactility/lvgl_module.h>

#include <lilygo/drivers/tpager_encoder_input.h>

constexpr auto* TAG = "T-Lora Pager";

extern "C" {

tt::kernel::SystemEventSubscription event_subscription;

static error_t start() {

    LOG_I(TAG, LOG_MESSAGE_POWER_ON_START);

    event_subscription = tt::kernel::subscribeSystemEvent(tt::kernel::SystemEvent::BootSplash, [](tt::kernel::SystemEvent) {
        // The kernel tpager_encoder device is already started by kernel_init(); this just
        // registers it as an LVGL input device, which requires LVGL to be up first.
        lvgl_lock();
        tpager_encoder::init();
        lvgl_unlock();

        auto gps_service = tt::service::gps::findGpsService();
        if (gps_service != nullptr) {
            std::vector<tt::hal::gps::GpsConfiguration> gps_configurations;
            gps_service->getGpsConfigurations(gps_configurations);
            if (gps_configurations.empty()) {
                if (gps_service->addGpsConfiguration(tt::hal::gps::GpsConfiguration {
                    .uartName = "uart0",
                    .baudRate = 38400,
                    .model = tt::hal::gps::GpsModel::UBLOX10
                })) {
                    LOG_I(TAG, "Configured internal GPS");
                } else {
                    LOG_E(TAG, "Failed to configure internal GPS");
                }
            }
        }
    });
    return ERROR_NONE;
}

static error_t stop() {
    tt::kernel::unsubscribeSystemEvent(event_subscription);
    return ERROR_NONE;
}

Module lilygo_tlora_pager_module = {
    .name = "lilygo-tlora-pager",
    .start = start,
    .stop = stop
};

}
