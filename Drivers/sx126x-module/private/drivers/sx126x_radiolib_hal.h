// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <RadioLib.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>

struct Device;

/**
 * RadioLib HAL on top of ESP-IDF GPIO and SPI master.
 *
 * RadioLib drives the chip-select manually across multiple transfers, so every
 * command/response exchange must be atomic on the bus. Two locks wrap each
 * exchange: the kernel SPI controller lock (spi_controller_lock) is the
 * abstraction other kernel drivers on this host serialise through, and ESP-IDF's
 * per-host bus lock (spi_device_acquire_bus) additionally blocks the IDF-managed
 * spi_master devices (display, SD) that don't take the controller lock. The
 * controller lock is taken as the outer lock; nothing else takes both, so there
 * is no lock-ordering hazard.
 */
class Sx126xRadiolibHal final : public RadioLibHal {
private:
    spi_host_device_t spiHostDevice;
    int spiFrequency;
    struct Device* spiController;
    spi_device_handle_t spiDeviceHandle = nullptr;
    bool spiInitialized = false;

public:
    Sx126xRadiolibHal(spi_host_device_t spiHostDevice, int spiFrequency, struct Device* spiController)
        : RadioLibHal(
              GPIO_MODE_INPUT,
              GPIO_MODE_OUTPUT,
              0, // LOW
              1, // HIGH
              GPIO_INTR_POSEDGE,
              GPIO_INTR_NEGEDGE
          )
        , spiHostDevice(spiHostDevice)
        , spiFrequency(spiFrequency)
        , spiController(spiController) {}

    void init() override;
    void term() override;

    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;

    void delay(unsigned long ms) override;
    void delayMicroseconds(unsigned long us) override;
    unsigned long millis() override;
    unsigned long micros() override;
    long pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) override;

    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
    void spiEndTransaction() override;
    void spiEnd() override;
};
