#ifdef ESP_PLATFORM

#pragma once

#include "SdCardDevice.h"

#include <Tactility/hal/spi/Spi.h>

#include <sd_protocol_types.h>
#include <utility>
#include <vector>
#include <hal/spi_types.h>
#include <soc/gpio_num.h>

namespace tt::hal::sdcard {

/**
 * SD card interface at the default SPI interface
 */
class SpiSdCardDevice final : public SdCardDevice {

public:

    struct Config {
        Config(
            gpio_num_t spiPinCs,
            gpio_num_t spiPinCd,
            gpio_num_t spiPinWp,
            gpio_num_t spiPinInt,
            MountBehaviour mountBehaviourAtBoot,
            /** When customLock is a nullptr, use the SPI default one */
            std::shared_ptr<Lock> customLock = nullptr,
            std::vector<gpio_num_t> csPinWorkAround = std::vector<gpio_num_t>(),
            spi_host_device_t spiHost = SPI2_HOST,
            int spiFrequencyKhz = SDMMC_FREQ_DEFAULT
        ) : spiFrequencyKhz(spiFrequencyKhz),
            spiPinCs(spiPinCs),
            spiPinCd(spiPinCd),
            spiPinWp(spiPinWp),
            spiPinInt(spiPinInt),
            mountBehaviourAtBoot(mountBehaviourAtBoot),
            customLock(customLock ? std::move(customLock) : nullptr),
            csPinWorkAround(std::move(csPinWorkAround)),
            spiHost(spiHost)
        {}

        int spiFrequencyKhz;
        gpio_num_t spiPinCs; // Clock
        gpio_num_t spiPinCd; // Card detect
        gpio_num_t spiPinWp; // Write-protect
        gpio_num_t spiPinInt; // Interrupt
        MountBehaviour mountBehaviourAtBoot;
        std::shared_ptr<Lock> customLock; // can be nullptr
        std::vector<gpio_num_t> csPinWorkAround;
        spi_host_device_t spiHost;
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

    explicit SpiSdCardDevice(std::unique_ptr<Config> config) : SdCardDevice(config->mountBehaviourAtBoot),
        config(std::move(config))
    {}

    std::string getName() const override { return "SD Card"; }
    std::string getDescription() const override { return "SD card via SPI interface"; }

    bool mount(const std::string& mountPath) override;
    bool unmount() override;
    std::string getMountPath() const override { return mountPath; }

    std::shared_ptr<Lock> getLock() const override {
        if (config->customLock != nullptr) {
            return config->customLock;
        } else {
            return spi::getLock(config->spiHost);
        }
    }

    State getState(TickType_t timeout) const override;

    /** return card when mounted, otherwise return nullptr */
    sdmmc_card_t* getCard() { return card; }
};

}

#endif
