#pragma once

#include "EspLcdDisplayV2.h"

#include <driver/spi_common.h>

#include <esp_lcd_io_spi.h>
#include <esp_lcd_types.h>

/**
 * Adds IO implementations on top of EspLcdDisplayV2
 * @warning This is an abstract class. You need to extend it to use it.
 */
class EspLcdSpiDisplay : public EspLcdDisplayV2 {

public:

    struct SpiConfiguration {
        spi_host_device_t spiHostDevice;
        gpio_num_t csPin;
        gpio_num_t dcPin;
        unsigned int pixelClockFrequency = 80'000'000; // Hertz
        size_t transactionQueueDepth = 10;
    };

    explicit EspLcdSpiDisplay(const std::shared_ptr<EspLcdConfiguration>& configuration, const std::shared_ptr<SpiConfiguration> spiConfiguration, int gammaCurveCount) :
        EspLcdDisplayV2(configuration),
        spiConfiguration(spiConfiguration),
        gammaCurveCount(gammaCurveCount)
    {}

private:

    std::shared_ptr<SpiConfiguration> spiConfiguration;
    int gammaCurveCount;

protected:

    bool createIoHandle(esp_lcd_panel_io_handle_t& ioHandle) override;

    // region Gamma

    void setGammaCurve(uint8_t index) override;

    uint8_t getGammaCurveCount() const override { return gammaCurveCount; }

    // endregion
};

