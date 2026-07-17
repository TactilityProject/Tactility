// SPDX-License-Identifier: Apache-2.0
#include "sx126x_radiolib_hal.h"

#include <tactility/delay.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/log.h>

#include <cstring>

#include <esp_rom_gpio.h>
#include <esp_timer.h>

#define TAG "sx126x_hal"

void Sx126xRadiolibHal::init() {
    spiBegin();
}

void Sx126xRadiolibHal::term() {
    spiEnd();
}

void Sx126xRadiolibHal::pinMode(uint32_t pin, uint32_t mode) {
    if (pin == RADIOLIB_NC) {
        return;
    }

    // Not gpio_config(): that rewrites the pin's interrupt type along with everything
    // else, and DIO1's HIGH_LEVEL interrupt is owned by the kernel GPIO descriptor API
    // while RadioLib still calls pinMode() on that pin during begin(). Configure the pad
    // routing, direction and pulls through the per-aspect setters instead, which leave
    // the interrupt configuration untouched.
    esp_rom_gpio_pad_select_gpio(pin);
    gpio_set_direction((gpio_num_t)pin, (gpio_mode_t)mode);
    gpio_set_pull_mode((gpio_num_t)pin, GPIO_FLOATING);
}

void Sx126xRadiolibHal::digitalWrite(uint32_t pin, uint32_t value) {
    if (pin == RADIOLIB_NC) {
        return;
    }

    gpio_set_level((gpio_num_t)pin, value);
}

uint32_t Sx126xRadiolibHal::digitalRead(uint32_t pin) {
    if (pin == RADIOLIB_NC) {
        return 0;
    }

    return gpio_get_level((gpio_num_t)pin);
}

void Sx126xRadiolibHal::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) {
    LOG_E(TAG, "Interrupt registration via RadioLib is not supported");
}

void Sx126xRadiolibHal::detachInterrupt(uint32_t interruptNum) {
    LOG_E(TAG, "Interrupt registration via RadioLib is not supported");
}

void Sx126xRadiolibHal::delay(unsigned long ms) {
    delay_millis(ms);
}

void Sx126xRadiolibHal::delayMicroseconds(unsigned long us) {
    delay_micros(us);
}

unsigned long Sx126xRadiolibHal::millis() {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

unsigned long Sx126xRadiolibHal::micros() {
    return (unsigned long)(esp_timer_get_time());
}

long Sx126xRadiolibHal::pulseIn(uint32_t pin, uint32_t state, unsigned long timeout) {
    if (pin == RADIOLIB_NC) {
        return 0;
    }

    this->pinMode(pin, GPIO_MODE_INPUT);
    uint32_t start = this->micros();
    uint32_t curtick = this->micros();

    while (this->digitalRead(pin) == state) {
        if ((this->micros() - curtick) > timeout) {
            return 0;
        }
    }

    return (this->micros() - start);
}

void Sx126xRadiolibHal::spiBegin() {
    if (!spiInitialized) {
        spi_device_interface_config_t devcfg = {};
        devcfg.clock_speed_hz = spiFrequency;
        devcfg.mode = 0;
        // CS is set to unused, as RadioLib sets it manually
        devcfg.spics_io_num = -1;
        devcfg.queue_size = 1;
        esp_err_t ret = spi_bus_add_device(spiHostDevice, &devcfg, &spiDeviceHandle);
        if (ret != ESP_OK) {
            LOG_E(TAG, "Failed to add SPI device, error %s", esp_err_to_name(ret));
        }
        spiInitialized = true;
    }
}

void Sx126xRadiolibHal::spiBeginTransaction() {
    // RadioLib holds CS low across multiple transfers, so the whole exchange must
    // be atomic on the bus. Take the kernel SPI controller lock (the arbiter other
    // kernel drivers on this host cooperate through) as the outer lock, then
    // ESP-IDF's per-host bus lock to also block the IDF-managed spi_master devices
    // (display, SD) that don't take the controller lock.
    spi_controller_lock(spiController);
    spi_device_acquire_bus(spiDeviceHandle, portMAX_DELAY);
}

void Sx126xRadiolibHal::spiTransfer(uint8_t* out, size_t len, uint8_t* in) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = out;
    t.rx_buffer = in;
    spi_device_polling_transmit(spiDeviceHandle, &t);
}

void Sx126xRadiolibHal::spiEndTransaction() {
    spi_device_release_bus(spiDeviceHandle);
    spi_controller_unlock(spiController);
}

void Sx126xRadiolibHal::spiEnd() {
    if (spiInitialized) {
        spi_bus_remove_device(spiDeviceHandle);
        spiInitialized = false;
    }
}
