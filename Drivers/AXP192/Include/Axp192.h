#pragma once

#include <axp192/axp192.h>
#include <tactility/device.h>
#include <Tactility/hal/power/PowerDevice.h>

#include <memory>

class Axp192 final : public tt::hal::power::PowerDevice {

    static int32_t i2cRead(void* handle, uint8_t address, uint8_t reg, uint8_t* buffer, uint16_t size);
    static int32_t i2cWrite(void* handle, uint8_t address, uint8_t reg, const uint8_t* buffer, uint16_t size);

public:

    struct Configuration {
        ::Device* controller;
        TickType_t readTimeout = 50 / portTICK_PERIOD_MS;
        TickType_t writeTimeout = 50 / portTICK_PERIOD_MS;
    };

private:

    std::unique_ptr<Configuration> configuration;

    axp192_t axpDevice = {
        .read = i2cRead,
        .write = i2cWrite,
        .handle = this
    };

    bool isInitialized = false;

public:

    explicit Axp192(std::unique_ptr<Configuration> configuration) : configuration(std::move(configuration)) {}
    ~Axp192() override {}

    /**
     * @warning Must call this function before device can operate!
     * @param onInit
     */
    bool init(const std::function<bool(axp192_t*)>& onInit) {
        isInitialized = onInit(&axpDevice);
        return isInitialized;
    }

    axp192_t* getAxp192() { return &axpDevice; }

    std::string getName() const override { return "AXP192"; }
    std::string getDescription() const override { return "AXP192 power management via I2C"; }

    bool supportsMetric(MetricType type) const override;
    bool getMetric(MetricType type, MetricData& data) override;

    bool supportsChargeControl() const override { return true; }
    bool isAllowedToCharge() const override;
    void setAllowedToCharge(bool canCharge) override;
};
