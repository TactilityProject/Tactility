#include <Tactility/Tactility.h>
#include <Tactility/hal/Configuration.h>
#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/SdCard.h>
#include <Tactility/hal/touch/TouchDevice.h>
#include <Tactility/kernel/SystemEvents.h>

#include <tactility/check.h>
#include <tactility/hal/Device.h>
#include <tactility/log.h>

namespace tt::hal {

constexpr auto* TAG = "Hal";

void registerDevices(const Configuration& configuration) {
    LOG_I(TAG, "Registering devices");

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
    LOG_I(TAG, "Starting displays & touch");
    auto displays = hal::findDevices<display::DisplayDevice>(Device::Type::Display);
    for (auto& display : displays) {
        LOG_I(TAG, "%s starting", display->getName().c_str());
        if (!display->start()) {
            LOG_E(TAG, "%s start failed", display->getName().c_str());
        } else {
            LOG_I(TAG, "%s started", display->getName().c_str());

            if (display->supportsBacklightDuty()) {
                LOG_I(TAG, "Setting backlight");
                display->setBacklightDuty(0);
            }

            auto touch = display->getTouchDevice();
            if (touch != nullptr) {
                LOG_I(TAG, "%s starting", touch->getName().c_str());
                if (!touch->start()) {
                    LOG_E(TAG, "%s start failed", touch->getName().c_str());
                } else {
                    LOG_I(TAG, "%s started", touch->getName().c_str());
                }
            }
        }
    }
}

void init(const Configuration& configuration) {
    kernel::publishSystemEvent(kernel::SystemEvent::BootInitHalBegin);

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
