#include <Tactility/Logger.h>
#include <Tactility/Tactility.h>
#include <tactility/check.h>
#include <Tactility/hal/Configuration.h>
#include <Tactility/hal/Device.h>
#include <Tactility/hal/gps/GpsInit.h>
#include <Tactility/hal/i2c/I2cInit.h>
#include <Tactility/hal/power/PowerDevice.h>
#include <Tactility/hal/spi/SpiInit.h>
#include <Tactility/hal/uart/UartInit.h>

#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/sdcard/SdCardMounting.h>
#include <Tactility/hal/touch/TouchDevice.h>
#include <Tactility/kernel/SystemEvents.h>

namespace tt::hal {

static const auto LOGGER = Logger("Hal");

void registerDevices(const Configuration& configuration) {
    LOGGER.info("Registering devices");

    auto devices = configuration.createDevices();
    for (auto& device : devices) {
        registerDevice(device);

        // Register attached devices
        if (device->getType() == Device::Type::Display) {
            const auto display = std::static_pointer_cast<display::DisplayDevice>(device);
            assert(display != nullptr);
            const std::shared_ptr<Device> touch = display->getTouchDevice();
            if (touch != nullptr) {
                registerDevice(touch);
            }
        }
    }
}

static void startDisplays() {
    LOGGER.info("Starting displays & touch");
    auto displays = hal::findDevices<display::DisplayDevice>(Device::Type::Display);
    for (auto& display : displays) {
        LOGGER.info("{} starting", display->getName());
        if (!display->start()) {
            LOGGER.error("{} start failed", display->getName());
        } else {
            LOGGER.info("{} started", display->getName());

            if (display->supportsBacklightDuty()) {
                LOGGER.info("Setting backlight");
                display->setBacklightDuty(0);
            }

            auto touch = display->getTouchDevice();
            if (touch != nullptr) {
                LOGGER.info("{} starting", touch->getName());
                if (!touch->start()) {
                    LOGGER.error("{} start failed", touch->getName());
                } else {
                    LOGGER.info("{} started", touch->getName());
                }
            }
        }
    }
}

void init(const Configuration& configuration) {
    kernel::publishSystemEvent(kernel::SystemEvent::BootInitHalBegin);

    kernel::publishSystemEvent(kernel::SystemEvent::BootInitI2cBegin);
    check(i2c::init(configuration.i2c), "I2C init failed");
    kernel::publishSystemEvent(kernel::SystemEvent::BootInitI2cEnd);

    kernel::publishSystemEvent(kernel::SystemEvent::BootInitSpiBegin);
    check(spi::init(configuration.spi), "SPI init failed");
    kernel::publishSystemEvent(kernel::SystemEvent::BootInitSpiEnd);

    kernel::publishSystemEvent(kernel::SystemEvent::BootInitUartBegin);
    check(uart::init(configuration.uart), "UART init failed");
    kernel::publishSystemEvent(kernel::SystemEvent::BootInitUartEnd);

    if (configuration.initBoot != nullptr) {
        check(configuration.initBoot(), "Init boot failed");
    }

    registerDevices(configuration);

    sdcard::mountAll(); // Warning: This needs to happen BEFORE displays are initialized on the SPI bus

    startDisplays(); // Warning: SPI displays need to start after SPI SD cards are mounted

    kernel::publishSystemEvent(kernel::SystemEvent::BootInitHalEnd);
}

const Configuration* getConfiguration() {
    return tt::getConfiguration()->hardware;
}

} // namespace
