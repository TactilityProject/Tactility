// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/concurrent/thread.h>
#include <tactility/drivers/lora.h>
#include <tactility/error.h>
#include <tactility/freertos/event_groups.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

struct Device;
struct GpioDescriptor;

/**
 * SX1262 radio engine: owns the radio thread, the TX queue and the callback lists.
 * The public methods are thread-safe. Callbacks are invoked with the internal mutex held,
 * either from the radio thread (RX, TX progress, state) or from the caller of transmit() (QUEUED).
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
    };

private:
    struct RadioParts;

    struct TxItem {
        LoraTxId id = 0;
        std::vector<uint8_t> data;
    };

    template<typename Callback>
    struct CallbackEntry {
        void* context;
        Callback callback;
    };

    const Settings settings;
    RadioParts* parts;
    mutable RecursiveMutex mutex = {};
    EventGroupHandle_t events = nullptr;

    Thread* thread = nullptr;
    bool threadInterrupted = false;

    enum LoraRadioState state = LORA_RADIO_STATE_OFF;
    enum LoraModulation modulation = LORA_MODULATION_NONE;

    std::deque<TxItem> txQueue;
    TxItem currentTx;
    LoraTxId lastTxId = 0;

    std::vector<CallbackEntry<LoraStateCallback>> stateCallbacks;
    std::vector<CallbackEntry<LoraRxCallback>> rxCallbacks;
    std::vector<CallbackEntry<LoraTxCallback>> txCallbacks;

    // Parameter store, applied on the next doBegin()
    int8_t power = -9;
    float frequency = 150;
    float bandwidth = 0.0;
    uint8_t spreadingFactor = 0;
    uint8_t codingRate = 0;
    uint8_t syncWord = 0;
    uint16_t preambleLength = 0;
    float bitRate = 0.0;
    float frequencyDeviation = 0.0;
    bool narrowGrid = false;
    bool boostedGain = false;
    // PA over-current protection limit in mA. Default matches RadioLib's fail-safe 60 mA,
    // which caps output below +22 dBm; a board-aware consumer can raise it (up to 140 mA).
    float currentLimit = 60.0;

    static void dio1Isr(void* context);
    static int32_t threadMainStatic(void* context);

    void lock() const { recursive_mutex_lock(&mutex); }
    void unlock() const { recursive_mutex_unlock(&mutex); }

    bool isThreadInterrupted() const;
    int32_t threadMain();

    void setState(enum LoraRadioState newState);
    void publishRx(const struct LoraRxPacket& packet);
    void publishTx(LoraTxId id, enum LoraTransmissionState txState);

    size_t getTxQueueSize() const;
    TxItem popNextQueuedTx();

    void registerDio1Isr();
    void unregisterDio1Isr();

    error_t setBaseParameter(enum LoraParameter parameter, float value);
    error_t setLoraParameter(enum LoraParameter parameter, float value);
    error_t setFskParameter(enum LoraParameter parameter, float value);
    error_t setLrFhssParameter(enum LoraParameter parameter, float value);
    error_t getBaseParameter(enum LoraParameter parameter, float* value) const;
    error_t getLoraParameter(enum LoraParameter parameter, float* value) const;
    error_t getFskParameter(enum LoraParameter parameter, float* value) const;
    error_t getLrFhssParameter(enum LoraParameter parameter, float* value) const;

    int doBegin(enum LoraModulation beginModulation);
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

    enum LoraRadioState getState() const;
    error_t setEnabled(bool enabled);
    error_t setModulation(enum LoraModulation newModulation);
    enum LoraModulation getModulation() const;

    bool canTransmit(enum LoraModulation withModulation) const {
        return (withModulation == LORA_MODULATION_FSK) ||
               (withModulation == LORA_MODULATION_LORA) ||
               (withModulation == LORA_MODULATION_LR_FHSS);
    }

    bool canReceive(enum LoraModulation withModulation) const {
        return (withModulation == LORA_MODULATION_FSK) || (withModulation == LORA_MODULATION_LORA);
    }

    error_t setParameter(enum LoraParameter parameter, float value);
    error_t getParameter(enum LoraParameter parameter, float* value) const;

    error_t transmit(const uint8_t* data, size_t length, LoraTxId* id);

    error_t addRxCallback(void* context, LoraRxCallback callback);
    error_t removeRxCallback(LoraRxCallback callback);
    error_t addStateCallback(void* context, LoraStateCallback callback);
    error_t removeStateCallback(LoraStateCallback callback);
    error_t addTxCallback(void* context, LoraTxCallback callback);
    error_t removeTxCallback(LoraTxCallback callback);
};
