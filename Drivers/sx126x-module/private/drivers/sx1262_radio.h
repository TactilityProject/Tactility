// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/concurrent/thread.h>
#include <tactility/drivers/radio.h>
#include <tactility/error.h>
#include <tactility/freertos/event_groups.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>

#include <tactility/std/deque.hpp>
#include <tactility/std/vector.hpp>

#include <cstddef>
#include <cstdint>

struct Device;
struct GpioDescriptor;

/**
 * SX1262 radio engine: owns the radio thread and the TX queue. The public methods are
 * thread-safe. State/RX/TX events are published via radio_event_emit(), either from the radio
 * thread (RX, TX progress, state) or from the caller of transmit() (QUEUED).
 *
 * The RadioLib types live behind the RadioParts indirection: RadioLib declares a global
 * `class Module` that collides with the kernel's `struct Module` when both are visible
 * in the same translation unit, so RadioLib headers must not leak out of the implementation.
 */
class Sx1262Radio final {
public:
    struct Settings {
        /** The kernel device, passed to callbacks */
        Device* device;
        /** The parent SPI controller device, for the SPI controller bus lock */
        Device* spi_controller;
        spi_host_device_t spi_host;
        int spi_frequency_hz;
        // CS/RESET/BUSY are native SoC GPIO numbers: the RadioLib HAL drives them directly
        // through ESP-IDF, so they can't sit behind an IO expander (unlike enable/antenna-select,
        // which the driver-registration layer resolves through the GPIO descriptor API).
        gpio_num_t pin_cs;
        gpio_num_t pin_reset;
        gpio_num_t pin_busy;
        /** DIO1 IRQ line, owned by the driver. The radio thread arms it as a
         *  HIGH_LEVEL one-shot via the GPIO descriptor callback API. */
        struct GpioDescriptor* dio1;
        float tcxo_voltage;
        bool use_regulator_ldo;
        bool dio2_rf_switch;
        /** RF switch RX enable, GPIO_NUM_NC when not wired */
        gpio_num_t pin_rx_enable;
    };

private:
    struct RadioParts;

    struct TxItem {
        RadioTxId id = 0;
        tt::std::vector<uint8_t> data;
    };

    /** Work that a caller hands to the radio thread, because RadioLib calls must stay on that thread */
    enum class Request {
        None,
        Rssi,
        Receiving,
        Present,
    };

    const Settings settings;
    RadioParts* parts;
    mutable RecursiveMutex mutex = {};
    EventGroupHandle_t events = nullptr;

    Mutex requestMutex = {};
    EventGroupHandle_t responses = nullptr;
    Request pendingRequest = Request::None;
    error_t requestResult = ERROR_NONE;
    float requestRssi = 0.0f;
    bool requestReceiving = false;

    Thread* thread = nullptr;
    bool threadInterrupted = false;

    enum RadioState state = RADIO_STATE_OFF;
    enum RadioModulation modulation = RADIO_MODULATION_NONE;

    tt::std::deque<TxItem> txQueue;
    TxItem currentTx;
    RadioTxId lastTxId = 0;

    // Parameter store, applied on the next doBegin(). Frequencies/rates are held in base SI
    // units (Hz, bit/s) and converted to RadioLib's MHz/kHz/kbps floats in doBegin().
    int8_t power = -9;
    int32_t frequency = 150000000; // Hz
    int32_t bandwidth = 0;         // Hz
    uint8_t spreadingFactor = 0;
    uint8_t codingRate = 0;
    uint8_t syncWord = 0;
    uint16_t preambleLength = 0;
    int32_t bitRate = 0;           // bit/s
    int32_t frequencyDeviation = 0; // Hz
    bool narrowGrid = false;
    bool boostedGain = false;
    // PA over-current protection limit in mA. Default matches RadioLib's fail-safe 60 mA,
    // which caps output below +22 dBm; a board-aware consumer can raise it (up to 140 mA).
    int32_t currentLimit = 60;     // mA
    bool lbt = false;
    int32_t lbtRssiThreshold = -90; // dBm

    static void dio1Isr(void* context);
    static int32_t threadMainStatic(void* context);

    void lock() const { recursive_mutex_lock(&mutex); }
    void unlock() const { recursive_mutex_unlock(&mutex); }

    bool isThreadInterrupted() const;
    int32_t threadMain();

    void setState(enum RadioState newState);
    error_t publishRx(const uint8_t* data, size_t length, float rssi, float snr);
    error_t publishTx(RadioTxId id, enum RadioTransmissionState txState);

    size_t getTxQueueSize() const;
    TxItem popNextQueuedTx();

    void registerDio1Isr();
    void unregisterDio1Isr();

    error_t performRequest(Request request);
    void handleRequest();
    bool isChannelBusy();
    bool waitForClearChannel();

    error_t setBaseParameter(enum RadioParameter parameter, int32_t value);
    error_t setLoraParameter(enum RadioParameter parameter, int32_t value);
    error_t setFskParameter(enum RadioParameter parameter, int32_t value);
    error_t setLrFhssParameter(enum RadioParameter parameter, int32_t value);
    error_t getBaseParameter(enum RadioParameter parameter, int32_t* value) const;
    error_t getLoraParameter(enum RadioParameter parameter, int32_t* value) const;
    error_t getFskParameter(enum RadioParameter parameter, int32_t* value) const;
    error_t getLrFhssParameter(enum RadioParameter parameter, int32_t* value) const;

    int doBegin(enum RadioModulation beginModulation);
    void doEnd();
    void doTransmit();
    bool doListen();
    void doReceive();

public:
    explicit Sx1262Radio(const Settings& settings);
    ~Sx1262Radio();

    /**
     * Verify a live SX1262 responds on the wired pins, using only GPIO (no SPI traffic):
     * pulse NRESET and expect the chip to drive BUSY low once it reaches standby.
     * @return ERROR_NONE when the chip responded
     */
    error_t probe() const;

    /**
     * Check that the chip still answers, for the hotplug probe. Only checked while the radio is on,
     * because only the radio thread may access the chip then.
     * @retval ERROR_NOT_FOUND the chip doesn't answer
     */
    error_t checkPresent();

    enum RadioState getState() const;
    error_t setEnabled(bool enabled);
    error_t setModulation(enum RadioModulation newModulation);
    enum RadioModulation getModulation() const;

    bool canTransmit(enum RadioModulation withModulation) const {
        return (withModulation == RADIO_MODULATION_FSK) ||
               (withModulation == RADIO_MODULATION_LORA) ||
               (withModulation == RADIO_MODULATION_LR_FHSS);
    }

    bool canReceive(enum RadioModulation withModulation) const {
        return (withModulation == RADIO_MODULATION_FSK) || (withModulation == RADIO_MODULATION_LORA);
    }

    error_t setParameter(enum RadioParameter parameter, int32_t value);
    error_t getParameter(enum RadioParameter parameter, int32_t* value) const;

    error_t transmit(const uint8_t* data, size_t length, RadioTxId* id);

    error_t getRssi(float* rssi);
    error_t isReceiving(bool* receiving);
};
