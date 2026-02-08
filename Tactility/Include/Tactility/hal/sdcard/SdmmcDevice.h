#ifdef ESP_PLATFORM

#pragma once

#include "SdCardDevice.h"

#include <Tactility/RecursiveMutex.h>
#include <tactility/hal/Device.h>
#include <sd_protocol_types.h>
#include <soc/gpio_num.h>
#include <utility>
#include <vector>

namespace tt::hal::sdcard {

/**
 * SD card interface for the SDMMC interface.
 */
class SdmmcDevice final : public SdCardDevice {

    std::shared_ptr<RecursiveMutex> mutex = std::make_shared<RecursiveMutex>();

public:

    struct Config {
        Config(
            gpio_num_t pinClock,
            gpio_num_t pinCmd,
            gpio_num_t pinD0,
            gpio_num_t pinD1,
            gpio_num_t pinD2,
            gpio_num_t pinD3,
            MountBehaviour mountBehaviourAtBoot,
            uint8_t busWidth = 4
        ) :
            pinClock(pinClock),
            pinCmd(pinCmd),
            pinD0(pinD0),
            pinD1(pinD1),
            pinD2(pinD2),
            pinD3(pinD3),
            mountBehaviourAtBoot(mountBehaviourAtBoot),
            busWidth(busWidth)
        {}

        int spiFrequencyKhz;
        gpio_num_t pinClock;
        gpio_num_t pinCmd;
        gpio_num_t pinD0;
        gpio_num_t pinD1;
        gpio_num_t pinD2;
        gpio_num_t pinD3;
        MountBehaviour mountBehaviourAtBoot;
        uint8_t busWidth;
        bool formatOnMountFailed = false;
        uint16_t maxOpenFiles = 4;
        uint16_t allocUnitSize = 16 * 1024;
        bool statusCheckEnabled = false;
    };

private:

    std::string mountPath;
    sdmmc_card_t* card = nullptr;
    std::shared_ptr<Config> config;

    bool applyGpioWorkAround();
    bool mountInternal(const std::string& mountPath);

public:

    explicit SdmmcDevice(std::unique_ptr<Config> config) : SdCardDevice(config->mountBehaviourAtBoot),
        config(std::move(config))
    {}

    std::string getName() const override { return "SDMMC"; }
    std::string getDescription() const override { return "SD card via SDMMC interface"; }

    bool mount(const std::string& mountPath) override;
    bool unmount() override;
    std::string getMountPath() const override { return mountPath; }

    std::shared_ptr<Lock> getLock() const override { return mutex; }

    State getState(TickType_t timeout) const override;

    /** return card when mounted, otherwise return nullptr */
    sdmmc_card_t* getCard() { return card; }
};

}

#endif
