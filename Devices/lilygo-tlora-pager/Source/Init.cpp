#include <Bq27220.h>
#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/kernel/SystemEvents.h>
#include <Tactility/service/gps/GpsService.h>
#include <Tactility/hal/gps/GpsConfiguration.h>

#include <driver/gpio.h>

#include <PwmBacklight.h>

static const auto LOGGER = tt::Logger("T-Lora Pager");

bool tpagerInit() {
    LOGGER.info(LOG_MESSAGE_POWER_ON_START);

    /* 32 Khz and higher gives an issue where the screen starts dimming again above 80% brightness
     * when moving the brightness slider rapidly from a lower setting to 100%.
     * This is not a slider bug (data was debug-traced) */
    if (!driver::pwmbacklight::init(GPIO_NUM_42, 30000)) {
        LOGGER.error("Backlight init failed");
        return false;
    }

    tt::kernel::subscribeSystemEvent(tt::kernel::SystemEvent::BootSplash, [](auto) {
        tt::hal::findDevices([](auto device) {
            if (device->getName() == "BQ27220") {
                auto bq27220 = std::reinterpret_pointer_cast<Bq27220>(device);
                if (bq27220 != nullptr) {
                    bq27220->configureCapacity(1500, 1500);
                    return false;
                }
            }

            return true;
        });

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
                    LOGGER.info("Configured internal GPS");
                } else {
                    LOGGER.error("Failed to configure internal GPS");
                }
            }
        }
    });

    return true;
}
