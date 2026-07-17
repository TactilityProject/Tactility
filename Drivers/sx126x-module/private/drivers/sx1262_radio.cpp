// SPDX-License-Identifier: Apache-2.0
#include "sx1262_radio.h"

#include "sx126x_radiolib_hal.h"

#include <tactility/concurrent/event_group.h>
#include <tactility/delay.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/log.h>

#include <algorithm>
#include <initializer_list>

#include <RadioLib.h>

#define TAG "sx1262"

namespace {

// TX-done wait is derived from the packet's time-on-air: fixed timeouts either
// false-time-out on slow configs (high SF / narrow BW, airtime up to seconds) or
// wait needlessly long on fast ones. The margin covers PA ramp and command latency;
// the fallback is used only when RadioLib can't compute airtime for the modem config.
constexpr auto SX1262_TX_TIMEOUT_MARGIN_MILLIS = 1000;
constexpr auto SX1262_TX_TIMEOUT_FALLBACK_MILLIS = 2000;
constexpr uint32_t SX1262_INTERRUPT_BIT = (1 << 0);
constexpr uint32_t SX1262_DIO1_EVENT_BIT = (1 << 1);
constexpr uint32_t SX1262_QUEUED_TX_BIT = (1 << 2);
constexpr auto SX1262_IRQ_FLAGS = RADIOLIB_IRQ_RX_DEFAULT_FLAGS;
// RX callbacks run on the radio thread and may do non-trivial work (e.g. packet decryption)
constexpr size_t SX1262_THREAD_STACK_SIZE = 8192;

const char* toString(enum LoraRadioState state) {
    switch (state) {
        case LORA_RADIO_STATE_OFF:
            return "off";
        case LORA_RADIO_STATE_ON_PENDING:
            return "on-pending";
        case LORA_RADIO_STATE_ON:
            return "on";
        case LORA_RADIO_STATE_OFF_PENDING:
            return "off-pending";
        case LORA_RADIO_STATE_ERROR:
            return "error";
        default:
            return "unknown";
    }
}

const char* toString(enum LoraModulation modulation) {
    switch (modulation) {
        case LORA_MODULATION_NONE:
            return "none";
        case LORA_MODULATION_FSK:
            return "FSK";
        case LORA_MODULATION_LORA:
            return "LoRa";
        case LORA_MODULATION_LR_FHSS:
            return "LR-FHSS";
        default:
            return "unknown";
    }
}

const char* toString(enum LoraParameter parameter) {
    switch (parameter) {
        case LORA_PARAMETER_POWER:
            return "power";
        case LORA_PARAMETER_BOOSTED_GAIN:
            return "boosted gain";
        case LORA_PARAMETER_FREQUENCY:
            return "frequency";
        case LORA_PARAMETER_BANDWIDTH:
            return "bandwidth";
        case LORA_PARAMETER_SPREADING_FACTOR:
            return "spreading factor";
        case LORA_PARAMETER_CODING_RATE:
            return "coding rate";
        case LORA_PARAMETER_SYNC_WORD:
            return "sync word";
        case LORA_PARAMETER_PREAMBLE_LENGTH:
            return "preamble length";
        case LORA_PARAMETER_FREQUENCY_DEVIATION:
            return "frequency deviation";
        case LORA_PARAMETER_DATA_RATE:
            return "data rate";
        case LORA_PARAMETER_NARROW_GRID:
            return "narrow grid";
        default:
            return "unknown";
    }
}

template<typename T>
constexpr error_t checkLimitsAndApply(T& target, const float value, const float lower, const float upper, const unsigned step = 0) {
    if ((value >= lower) && (value <= upper)) {
        if (step != 0) {
            int ivalue = static_cast<int>(value);
            if ((ivalue % step) != 0) {
                return ERROR_OUT_OF_RANGE;
            }
        }

        target = static_cast<T>(value);
        return ERROR_NONE;
    }
    return ERROR_OUT_OF_RANGE;
}

template<typename T>
constexpr error_t checkValuesAndApply(T& target, const float value, std::initializer_list<float> valids) {
    for (float valid : valids) {
        if (value == valid) {
            target = static_cast<T>(value);
            return ERROR_NONE;
        }
    }
    return ERROR_OUT_OF_RANGE;
}

} // namespace

struct Sx1262Radio::RadioParts {
    Sx126xRadiolibHal hal;
    Module radioModule;
    SX1262 radio;

    explicit RadioParts(const Settings& settings)
        : hal(settings.spi_host, settings.spi_frequency_hz, settings.spi_controller)
        , radioModule(&hal, settings.pin_cs, RADIOLIB_NC, settings.pin_reset, settings.pin_busy)
        , radio(&radioModule) {}
};

Sx1262Radio::Sx1262Radio(const Settings& settings)
    : settings(settings) {
    recursive_mutex_construct(&mutex);
    event_group_construct(&events);
    parts = new RadioParts(settings);
}

Sx1262Radio::~Sx1262Radio() {
    setEnabled(false);
    delete parts;
    event_group_destruct(&events);
    recursive_mutex_destruct(&mutex);
}

error_t Sx1262Radio::probe() const {
    // NRESET output, idle high. BUSY input with a pull-up: an absent or unpowered
    // module leaves BUSY floating, and the pull-up parks it high so the ready
    // check below can't false-pass on a floating line.
    gpio_config_t reset_conf = {
        .pin_bit_mask = (1ULL << settings.pin_reset),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&reset_conf);
    gpio_set_level(settings.pin_reset, 1);

    gpio_config_t busy_conf = {
        .pin_bit_mask = (1ULL << settings.pin_busy),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&busy_conf);

    // Reset pulse (datasheet: NRESET low for >= 100 us triggers a full reset)
    gpio_set_level(settings.pin_reset, 0);
    delay_millis(2);
    gpio_set_level(settings.pin_reset, 1);

    // After reset the chip boots and calibrates with BUSY high, then drives BUSY
    // low once it reaches STDBY_RC (datasheet: ~3.5 ms max). Allow a generous
    // margin; a line stuck high means no chip is answering.
    constexpr auto PROBE_TIMEOUT_MILLIS = 20;
    int elapsed = 0;
    while (gpio_get_level(settings.pin_busy) != 0) {
        if (elapsed >= PROBE_TIMEOUT_MILLIS) {
            LOG_E(TAG, "Probe failed: BUSY (GPIO %d) stuck high after reset — module absent or unpowered?", settings.pin_busy);
            return ERROR_RESOURCE;
        }
        delay_millis(1);
        elapsed++;
    }

    // Drop the probe pull-up again: the chip actively drives BUSY when powered,
    // and RadioLib reconfigures the pin at begin() anyway.
    busy_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&busy_conf);

    LOG_I(TAG, "Probe OK: SX1262 answered reset in ~%d ms (BUSY low)", elapsed);
    return ERROR_NONE;
}

// region Thread lifecycle

void Sx1262Radio::dio1Isr(void* context) {
    auto* self = static_cast<Sx1262Radio*>(context);
    // DIO1 is armed as a HIGH_LEVEL interrupt (edge types are unreliable on the
    // ESP32 per erratum 3.11). A level interrupt re-fires for as long as the line
    // is asserted, so mask it here and let the radio thread re-arm once it has
    // cleared the modem IRQ (which drops DIO1 low again).
    gpio_descriptor_disable_interrupt(self->settings.dio1);
    event_group_set(self->events, SX1262_DIO1_EVENT_BIT);
}

int32_t Sx1262Radio::threadMainStatic(void* context) {
    return static_cast<Sx1262Radio*>(context)->threadMain();
}

bool Sx1262Radio::isThreadInterrupted() const {
    lock();
    const bool interrupted = threadInterrupted;
    unlock();
    return interrupted;
}

int32_t Sx1262Radio::threadMain() {
    int rc = doBegin(getModulation());
    bool hasRx = false;
    if (rc != 0) {
        return rc;
    }
    setState(LORA_RADIO_STATE_ON);

    while (!isThreadInterrupted()) {
        // Re-arm DIO1: the ISR masks the HIGH_LEVEL interrupt on each fire, so the
        // modem's next RX/TX-done needs it enabled again. DIO1 is low here (the
        // previous IRQ was cleared by doReceive()/doTransmit()); re-arming while it
        // were still asserted would just self-fire once and be absorbed by the
        // empty-read guard in doReceive().
        gpio_descriptor_enable_interrupt(settings.dio1);

        hasRx = doListen();

        // Thread might've been interrupted in the meanwhile
        if (isThreadInterrupted()) {
            break;
        }

        // Service a received packet before deciding to transmit: an RX-done and a
        // queued TX can coincide in the same iteration, and dropping the RX here would
        // lose the packet outright.
        if (hasRx) {
            doReceive();
        }
        if (getTxQueueSize() > 0) {
            doTransmit();
        }
    }

    doEnd();
    return 0;
}

error_t Sx1262Radio::setEnabled(bool enabled) {
    lock();

    if (enabled) {
        if ((thread != nullptr) && (thread_get_state(thread) != THREAD_STATE_STOPPED)) {
            LOG_W(TAG, "Already started");
            unlock();
            return ERROR_NONE;
        }

        if (modulation == LORA_MODULATION_NONE) {
            LOG_E(TAG, "Cannot enable without a modulation set");
            unlock();
            return ERROR_INVALID_STATE;
        }

        if (thread != nullptr) {
            thread_free(thread);
            thread = nullptr;
        }

        threadInterrupted = false;
        setState(LORA_RADIO_STATE_ON_PENDING);

        thread = thread_alloc_full("SX1262", SX1262_THREAD_STACK_SIZE, threadMainStatic, this, tskNO_AFFINITY);
        if (thread == nullptr) {
            setState(LORA_RADIO_STATE_ERROR);
            unlock();
            return ERROR_OUT_OF_MEMORY;
        }
        thread_set_priority(thread, THREAD_PRIORITY_HIGH);

        if (thread_start(thread) != ERROR_NONE) {
            thread_free(thread);
            thread = nullptr;
            setState(LORA_RADIO_STATE_ERROR);
            unlock();
            return ERROR_UNDEFINED;
        }

        unlock();
        return ERROR_NONE;
    } else {
        setState(LORA_RADIO_STATE_OFF_PENDING);

        if (thread != nullptr) {
            threadInterrupted = true;
            event_group_set(events, SX1262_INTERRUPT_BIT);

            Thread* oldThread = thread;
            thread = nullptr;

            if (thread_get_state(oldThread) != THREAD_STATE_STOPPED) {
                // Unlock so the thread can lock
                unlock();
                // Wait for the thread to finish
                thread_join(oldThread, portMAX_DELAY, pdMS_TO_TICKS(10));
                // Re-lock to continue logic below
                lock();
            }

            thread_free(oldThread);
        }

        setState(LORA_RADIO_STATE_OFF);
        unlock();
        return ERROR_NONE;
    }
}

// endregion

// region State, modulation and callbacks

enum LoraRadioState Sx1262Radio::getState() const {
    lock();
    const auto result = state;
    unlock();
    return result;
}

void Sx1262Radio::setState(enum LoraRadioState newState) {
    lock();
    if (state == newState) {
        unlock();
        return;
    }
    LOG_I(TAG, "State: %s -> %s", toString(state), toString(newState));
    state = newState;
    auto callbacks = stateCallbacks;
    unlock();

    for (const auto& entry : callbacks) {
        entry.callback(settings.device, entry.context, newState);
    }
}

error_t Sx1262Radio::setModulation(enum LoraModulation newModulation) {
    const auto currentState = getState();
    if ((currentState == LORA_RADIO_STATE_ON_PENDING) || (currentState == LORA_RADIO_STATE_ON)) {
        return ERROR_INVALID_STATE;
    }

    if (!((newModulation == LORA_MODULATION_NONE) || canTransmit(newModulation) || canReceive(newModulation))) {
        return ERROR_NOT_SUPPORTED;
    }

    lock();
    LOG_I(TAG, "Modulation set to %s", toString(newModulation));
    modulation = newModulation;
    unlock();
    return ERROR_NONE;
}

enum LoraModulation Sx1262Radio::getModulation() const {
    lock();
    const auto result = modulation;
    unlock();
    return result;
}

// Callbacks are invoked on a snapshot of the list, with the radio mutex released:
// consumers take their own locks in callbacks and also call into this API while
// holding those locks, so invoking under the radio mutex would set up an AB-BA
// deadlock between the radio thread and any consumer thread.
void Sx1262Radio::publishRx(const struct LoraRxPacket& packet) {
    lock();
    auto callbacks = rxCallbacks;
    unlock();

    for (const auto& entry : callbacks) {
        entry.callback(settings.device, entry.context, &packet);
    }
}

void Sx1262Radio::publishTx(LoraTxId id, enum LoraTransmissionState txState) {
    lock();
    auto callbacks = txCallbacks;
    unlock();

    for (const auto& entry : callbacks) {
        entry.callback(settings.device, entry.context, id, txState);
    }
}

error_t Sx1262Radio::addRxCallback(void* context, LoraRxCallback callback) {
    lock();
    rxCallbacks.push_back({context, callback});
    unlock();
    return ERROR_NONE;
}

error_t Sx1262Radio::removeRxCallback(LoraRxCallback callback) {
    lock();
    const auto old_size = rxCallbacks.size();
    std::erase_if(rxCallbacks, [callback](const auto& entry) { return entry.callback == callback; });
    const auto result = (rxCallbacks.size() == old_size) ? ERROR_NOT_FOUND : ERROR_NONE;
    unlock();
    return result;
}

error_t Sx1262Radio::addStateCallback(void* context, LoraStateCallback callback) {
    lock();
    stateCallbacks.push_back({context, callback});
    unlock();
    return ERROR_NONE;
}

error_t Sx1262Radio::removeStateCallback(LoraStateCallback callback) {
    lock();
    const auto old_size = stateCallbacks.size();
    std::erase_if(stateCallbacks, [callback](const auto& entry) { return entry.callback == callback; });
    const auto result = (stateCallbacks.size() == old_size) ? ERROR_NOT_FOUND : ERROR_NONE;
    unlock();
    return result;
}

error_t Sx1262Radio::addTxCallback(void* context, LoraTxCallback callback) {
    lock();
    txCallbacks.push_back({context, callback});
    unlock();
    return ERROR_NONE;
}

error_t Sx1262Radio::removeTxCallback(LoraTxCallback callback) {
    lock();
    const auto old_size = txCallbacks.size();
    std::erase_if(txCallbacks, [callback](const auto& entry) { return entry.callback == callback; });
    const auto result = (txCallbacks.size() == old_size) ? ERROR_NOT_FOUND : ERROR_NONE;
    unlock();
    return result;
}

// endregion

// region TX queue

size_t Sx1262Radio::getTxQueueSize() const {
    lock();
    const auto size = txQueue.size();
    unlock();
    return size;
}

Sx1262Radio::TxItem Sx1262Radio::popNextQueuedTx() {
    lock();
    auto tx = std::move(txQueue.front());
    txQueue.pop_front();
    unlock();
    return tx;
}

error_t Sx1262Radio::transmit(const uint8_t* data, size_t length, LoraTxId* id) {
    lock();
    const auto txId = lastTxId;
    lastTxId++;
    txQueue.push_back(TxItem {.id = txId, .data = std::vector<uint8_t>(data, data + length)});
    LOG_D(TAG, "TX id=%d queued: %u bytes (queue depth %u)", (int)txId, (unsigned)length, (unsigned)txQueue.size());
    unlock();

    publishTx(txId, LORA_TRANSMISSION_STATE_QUEUED);
    event_group_set(events, SX1262_QUEUED_TX_BIT);

    if (id != nullptr) {
        *id = txId;
    }
    return ERROR_NONE;
}

// endregion

// region Parameters

error_t Sx1262Radio::setBaseParameter(enum LoraParameter parameter, float value) {
    switch (parameter) {
        case LORA_PARAMETER_POWER:
            return checkLimitsAndApply(power, value, -9.0, 22.0);
        case LORA_PARAMETER_BOOSTED_GAIN:
            return checkLimitsAndApply(boostedGain, value, 0.0, 1.0, 1);
        case LORA_PARAMETER_CURRENT_LIMIT:
            // SX1262 OCP range is 0..140 mA (RadioLib clamps to a 2.5 mA step internally).
            return checkLimitsAndApply(currentLimit, value, 0.0, 140.0);
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

error_t Sx1262Radio::setLoraParameter(enum LoraParameter parameter, float value) {
    switch (parameter) {
        case LORA_PARAMETER_FREQUENCY:
            return checkLimitsAndApply(frequency, value, 150.0, 960.0);
        case LORA_PARAMETER_BANDWIDTH:
            return checkValuesAndApply(bandwidth, value, {7.8, 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125.0, 250.0, 500.0});
        case LORA_PARAMETER_SPREADING_FACTOR:
            return checkLimitsAndApply(spreadingFactor, value, 7.0, 12.0, 1);
        case LORA_PARAMETER_CODING_RATE:
            return checkLimitsAndApply(codingRate, value, 5.0, 8.0, 1);
        case LORA_PARAMETER_SYNC_WORD:
            return checkLimitsAndApply(syncWord, value, 0.0, 255.0, 1);
        case LORA_PARAMETER_PREAMBLE_LENGTH:
            return checkLimitsAndApply(preambleLength, value, 0.0, 65535.0, 1);
        default:
            break;
    }

    LOG_W(TAG, "Tried to set unsupported LoRa parameter \"%s\" to %f", toString(parameter), value);
    return ERROR_NOT_SUPPORTED;
}

error_t Sx1262Radio::setFskParameter(enum LoraParameter parameter, float value) {
    switch (parameter) {
        case LORA_PARAMETER_FREQUENCY:
            return checkLimitsAndApply(frequency, value, 150.0, 960.0);
        case LORA_PARAMETER_BANDWIDTH:
            return checkValuesAndApply(bandwidth, value, {4.8, 5.8, 7.3, 9.7, 11.7, 14.6, 19.5, 23.4, 29.3, 39.0, 46.9, 58.6, 78.2});
        case LORA_PARAMETER_PREAMBLE_LENGTH:
            return checkLimitsAndApply(preambleLength, value, 0.0, 65535.0, 1);
        case LORA_PARAMETER_DATA_RATE:
            return checkLimitsAndApply(bitRate, value, 0.6, 300.0);
        case LORA_PARAMETER_FREQUENCY_DEVIATION:
            return checkLimitsAndApply(frequencyDeviation, value, 0.0, 200.0);
        default:
            break;
    }

    LOG_W(TAG, "Tried to set unsupported FSK parameter \"%s\" to %f", toString(parameter), value);
    return ERROR_NOT_SUPPORTED;
}

error_t Sx1262Radio::setLrFhssParameter(enum LoraParameter parameter, float value) {
    switch (parameter) {
        case LORA_PARAMETER_BANDWIDTH:
            return checkValuesAndApply(bandwidth, value, {39.06, 85.94, 136.72, 183.59, 335.94, 386.72, 722.66, 773.44, 1523.4, 1574.2});
        case LORA_PARAMETER_CODING_RATE:
            return checkValuesAndApply(codingRate, value, {RADIOLIB_SX126X_LR_FHSS_CR_5_6, RADIOLIB_SX126X_LR_FHSS_CR_2_3, RADIOLIB_SX126X_LR_FHSS_CR_1_2, RADIOLIB_SX126X_LR_FHSS_CR_1_3});
        case LORA_PARAMETER_NARROW_GRID:
            return checkLimitsAndApply(narrowGrid, value, 0.0, 1.0, 1);
        default:
            break;
    }

    LOG_W(TAG, "Tried to set unsupported LR-FHSS parameter \"%s\" to %f", toString(parameter), value);
    return ERROR_NOT_SUPPORTED;
}

error_t Sx1262Radio::setParameter(enum LoraParameter parameter, float value) {
    lock();

    error_t result = setBaseParameter(parameter, value);
    if (result == ERROR_NOT_SUPPORTED) {
        switch (modulation) {
            case LORA_MODULATION_LORA:
                result = setLoraParameter(parameter, value);
                break;
            case LORA_MODULATION_FSK:
                result = setFskParameter(parameter, value);
                break;
            case LORA_MODULATION_LR_FHSS:
                result = setLrFhssParameter(parameter, value);
                break;
            default:
                break;
        }
    }

    if (result == ERROR_NONE) {
        LOG_D(TAG, "Parameter %s = %f", toString(parameter), value);
    }

    unlock();
    return result;
}

error_t Sx1262Radio::getBaseParameter(enum LoraParameter parameter, float* value) const {
    switch (parameter) {
        case LORA_PARAMETER_POWER:
            *value = power;
            return ERROR_NONE;
        case LORA_PARAMETER_BOOSTED_GAIN:
            *value = boostedGain;
            return ERROR_NONE;
        case LORA_PARAMETER_CURRENT_LIMIT:
            *value = currentLimit;
            return ERROR_NONE;
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

error_t Sx1262Radio::getLoraParameter(enum LoraParameter parameter, float* value) const {
    switch (parameter) {
        case LORA_PARAMETER_FREQUENCY:
            *value = frequency;
            return ERROR_NONE;
        case LORA_PARAMETER_BANDWIDTH:
            *value = bandwidth;
            return ERROR_NONE;
        case LORA_PARAMETER_SPREADING_FACTOR:
            *value = spreadingFactor;
            return ERROR_NONE;
        case LORA_PARAMETER_CODING_RATE:
            *value = codingRate;
            return ERROR_NONE;
        case LORA_PARAMETER_SYNC_WORD:
            *value = syncWord;
            return ERROR_NONE;
        case LORA_PARAMETER_PREAMBLE_LENGTH:
            *value = preambleLength;
            return ERROR_NONE;
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

error_t Sx1262Radio::getFskParameter(enum LoraParameter parameter, float* value) const {
    switch (parameter) {
        case LORA_PARAMETER_FREQUENCY:
            *value = frequency;
            return ERROR_NONE;
        case LORA_PARAMETER_BANDWIDTH:
            *value = bandwidth;
            return ERROR_NONE;
        case LORA_PARAMETER_DATA_RATE:
            *value = bitRate;
            return ERROR_NONE;
        case LORA_PARAMETER_FREQUENCY_DEVIATION:
            *value = frequencyDeviation;
            return ERROR_NONE;
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

error_t Sx1262Radio::getLrFhssParameter(enum LoraParameter parameter, float* value) const {
    switch (parameter) {
        case LORA_PARAMETER_BANDWIDTH:
            *value = bandwidth;
            return ERROR_NONE;
        case LORA_PARAMETER_CODING_RATE:
            *value = codingRate;
            return ERROR_NONE;
        case LORA_PARAMETER_NARROW_GRID:
            *value = narrowGrid;
            return ERROR_NONE;
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

error_t Sx1262Radio::getParameter(enum LoraParameter parameter, float* value) const {
    lock();

    // No warnings are emitted to be able to discover parameters by return status
    error_t result = getBaseParameter(parameter, value);
    if (result == ERROR_NOT_SUPPORTED) {
        switch (modulation) {
            case LORA_MODULATION_LORA:
                result = getLoraParameter(parameter, value);
                break;
            case LORA_MODULATION_FSK:
                result = getFskParameter(parameter, value);
                break;
            case LORA_MODULATION_LR_FHSS:
                result = getLrFhssParameter(parameter, value);
                break;
            default:
                break;
        }
    }

    unlock();
    return result;
}

// endregion

// region Radio operations (radio thread only)

// DIO1 uses the GPIO descriptor callback API in HIGH_LEVEL mode. The interrupt is
// armed (enabled) per cycle by the radio thread loop and masked by the ISR on each
// fire; the modem asserts DIO1 for RX/TX-done, which the driver clears by reading
// the packet or finishing the transmission. Edge-triggered interrupts are avoided
// on purpose (ESP32 erratum 3.11: subsequent edge interrupts may be missed, which
// for a radio would drop an RX/TX-done and stall the thread).
void Sx1262Radio::registerDio1Isr() {
    gpio_flags_t flags = GPIO_FLAG_DIRECTION_INPUT;
    flags = GPIO_FLAG_INTERRUPT_TO_OPTIONS(flags, GPIO_INTERRUPT_HIGH_LEVEL);
    if (gpio_descriptor_set_flags(settings.dio1, flags) != ERROR_NONE ||
        gpio_descriptor_add_callback(settings.dio1, dio1Isr, this) != ERROR_NONE) {
        LOG_E(TAG, "Failed to install DIO1 interrupt");
    }
}

void Sx1262Radio::unregisterDio1Isr() {
    gpio_descriptor_disable_interrupt(settings.dio1);
    gpio_descriptor_remove_callback(settings.dio1);
}

int Sx1262Radio::doBegin(enum LoraModulation beginModulation) {
    int16_t rc = RADIOLIB_ERR_NONE;
    auto& radio = parts->radio;

    if (beginModulation == LORA_MODULATION_LORA) {
        LOG_I(
            TAG,
            "Starting LoRa: %.3f MHz, BW %.2f kHz, SF%u, CR 4/%u, sync 0x%02X, preamble %u, %d dBm, TCXO %.1f V",
            frequency,
            bandwidth,
            spreadingFactor,
            codingRate,
            syncWord,
            preambleLength,
            power,
            settings.tcxo_voltage
        );
        rc = radio.begin(
            frequency,
            bandwidth,
            spreadingFactor,
            codingRate,
            syncWord,
            power,
            preambleLength,
            settings.tcxo_voltage,
            settings.use_regulator_ldo
        );
    } else if (beginModulation == LORA_MODULATION_FSK) {
        LOG_I(
            TAG,
            "Starting FSK: %.3f MHz, %.2f kbps, deviation %.1f kHz, BW %.1f kHz, preamble %u, %d dBm",
            frequency,
            bitRate,
            frequencyDeviation,
            bandwidth,
            preambleLength,
            power
        );
        rc = radio.beginFSK(
            frequency,
            bitRate,
            frequencyDeviation,
            bandwidth,
            power,
            preambleLength,
            settings.tcxo_voltage,
            settings.use_regulator_ldo
        );
    } else if (beginModulation == LORA_MODULATION_LR_FHSS) {
        LOG_I(TAG, "Starting LR-FHSS: BW %.2f kHz, CR %u, %s grid", bandwidth, codingRate, narrowGrid ? "narrow" : "wide");
        rc = radio.beginLRFHSS(
            bandwidth,
            codingRate,
            narrowGrid,
            settings.tcxo_voltage,
            settings.use_regulator_ldo
        );
    } else {
        LOG_E(TAG, "SX1262 not capable of modulation \"%s\"", toString(beginModulation));
        setState(LORA_RADIO_STATE_ERROR);
        return -1;
    }

    if (rc != RADIOLIB_ERR_NONE) {
        LOG_E(TAG, "RadioLib initialization failed with code %hi", rc);
        setState(LORA_RADIO_STATE_ERROR);
        return -1;
    }

    // Apply the PA over-current protection limit. RadioLib's begin() already set its
    // fail-safe default (60 mA), so this is only meaningful when a consumer raised it
    // via LORA_PARAMETER_CURRENT_LIMIT to reach higher output power.
    rc = radio.setCurrentLimit(currentLimit);
    if (rc != RADIOLIB_ERR_NONE) {
        LOG_E(TAG, "Setting current limit to %.1f mA failed with code %hi", currentLimit, rc);
        setState(LORA_RADIO_STATE_ERROR);
        return -1;
    }

    // Modules that wire the antenna TX/RX switch to DIO2 (e.g. LilyGO T-Deck Max)
    // must enable this or the RF path stays disconnected and no TX/RX gets through.
    if (settings.dio2_rf_switch) {
        rc = radio.setDio2AsRfSwitch(true);
        if (rc != RADIOLIB_ERR_NONE) {
            LOG_E(TAG, "Setting DIO2 as RF switch failed with code %hi", rc);
            setState(LORA_RADIO_STATE_ERROR);
            return -1;
        }
    }

    rc = radio.setRxBoostedGainMode(boostedGain, true);
    if (rc != RADIOLIB_ERR_NONE) {
        LOG_E(TAG, "Setting RX boosted gain to %s failed with code %hi", boostedGain ? "true" : "false", rc);
        setState(LORA_RADIO_STATE_ERROR);
        return -1;
    }

    LOG_I(TAG, "Modem initialized (chip verified by RadioLib)");
    registerDio1Isr();
    return 0;
}

void Sx1262Radio::doEnd() {
    unregisterDio1Isr();
    // Leave the modem in its lowest-power state; the next enable runs a full begin()
    const int16_t rc = parts->radio.sleep();
    if (rc != RADIOLIB_ERR_NONE) {
        LOG_W(TAG, "Putting modem to sleep failed with code %hi", rc);
    } else {
        LOG_I(TAG, "Modem put to sleep");
    }
}

void Sx1262Radio::doTransmit() {
    currentTx = popNextQueuedTx();
    auto& radio = parts->radio;

    int16_t rc = radio.standby();
    if (rc != RADIOLIB_ERR_NONE) {
        LOG_W(TAG, "RadioLib returned %hi on TX standby", rc);
    }

    LOG_I(TAG, "TX id=%d: %u bytes (%u more queued)", (int)currentTx.id, (unsigned)currentTx.data.size(), (unsigned)getTxQueueSize());
    rc = radio.startTransmit(currentTx.data.data(), currentTx.data.size());

    if (rc == RADIOLIB_ERR_NONE) {
        publishTx(currentTx.id, LORA_TRANSMISSION_STATE_TRANSMIT_PENDING);

        // Time-on-air (microseconds) for the current modem config; 0 if RadioLib can't
        // compute it, in which case fall back to a fixed timeout.
        const uint32_t airtimeMillis = radio.getTimeOnAir(currentTx.data.size()) / 1000;
        const uint32_t txTimeoutMillis = (airtimeMillis > 0)
            ? (airtimeMillis + SX1262_TX_TIMEOUT_MARGIN_MILLIS)
            : SX1262_TX_TIMEOUT_FALLBACK_MILLIS;

        // outFlags stays 0 on timeout, which routes to the Timeout branch below
        uint32_t txEventFlags = 0;
        event_group_wait(
            events,
            SX1262_INTERRUPT_BIT | SX1262_DIO1_EVENT_BIT,
            false,
            true,
            &txEventFlags,
            pdMS_TO_TICKS(txTimeoutMillis)
        );

        // Clean up after transmission
        radio.finishTransmit();

        // Thread might've been interrupted in the meanwhile. Publish a terminal state so a
        // caller that queued this TX still gets a final callback for its id when
        // setEnabled(false) races with an in-flight transmit.
        if (isThreadInterrupted()) {
            publishTx(currentTx.id, LORA_TRANSMISSION_STATE_ERROR);
            return;
        }

        // If the DIO1 bit is unset, this means the wait timed out
        if (txEventFlags & SX1262_DIO1_EVENT_BIT) {
            LOG_I(TAG, "TX id=%d: done", (int)currentTx.id);
            publishTx(currentTx.id, LORA_TRANSMISSION_STATE_TRANSMITTED);
        } else {
            LOG_W(TAG, "TX id=%d: no TX-done IRQ within %u ms", (int)currentTx.id, (unsigned)txTimeoutMillis);
            publishTx(currentTx.id, LORA_TRANSMISSION_STATE_TIMEOUT);
        }
    } else {
        LOG_E(TAG, "Error transmitting id=%d, rc=%hi", (int)currentTx.id, rc);
        publishTx(currentTx.id, LORA_TRANSMISSION_STATE_ERROR);
    }
}

bool Sx1262Radio::doListen() {
    auto& radio = parts->radio;

    if (getModulation() != LORA_MODULATION_LR_FHSS) {
        int16_t rc = radio.startReceiveDutyCycleAuto(preambleLength, 0, SX1262_IRQ_FLAGS);
        if (rc == RADIOLIB_ERR_NONE) {
            uint32_t flags = 0;
            event_group_wait(
                events,
                SX1262_INTERRUPT_BIT | SX1262_DIO1_EVENT_BIT | SX1262_QUEUED_TX_BIT,
                false,
                true,
                &flags,
                portMAX_DELAY
            );
            return (flags & SX1262_DIO1_EVENT_BIT) != 0;
        } else {
            LOG_E(TAG, "Error setting dutycycle RX, RadioLib returned %hi", rc);
        }
        return false;
    } else {
        // LR-FHSS modem only supports TX
        event_group_wait(
            events,
            SX1262_INTERRUPT_BIT | SX1262_QUEUED_TX_BIT,
            false,
            true,
            nullptr,
            portMAX_DELAY
        );
        return false;
    }
}

void Sx1262Radio::doReceive() {
    // LR-FHSS modem only supports TX
    if (getModulation() == LORA_MODULATION_LR_FHSS) return;

    auto& radio = parts->radio;

    uint16_t rxSize = radio.getPacketLength(true);
    std::vector<uint8_t> data(rxSize);
    int16_t rc = radio.readData(data.data(), rxSize);
    if (rc != RADIOLIB_ERR_NONE) {
        LOG_E(TAG, "Error receiving data, RadioLib returned %hi", rc);
    } else if (rxSize == 0) {
        // Empty read: skip silently to avoid log flooding on spurious IRQs.
    } else {
        const struct LoraRxPacket packet = {
            .data = data.data(),
            .length = data.size(),
            .rssi = radio.getRSSI(),
            .snr = radio.getSNR(),
        };

        LOG_I(TAG, "RX: %u bytes, RSSI %.1f dBm, SNR %.1f dB", (unsigned)packet.length, packet.rssi, packet.snr);
        publishRx(packet);
        radio.finishReceive();
    }
}

// endregion
