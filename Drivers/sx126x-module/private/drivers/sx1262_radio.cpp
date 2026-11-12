// SPDX-License-Identifier: Apache-2.0
#include "sx1262_radio.h"

#include "sx126x_radiolib_hal.h"

#include <tactility/Memory.hpp>

#include <tactility/check.h>
#include <tactility/concurrent/event_group.h>
#include <tactility/delay.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/log.h>

#include <cstring>
#include <initializer_list>

#include <RadioLib.h>

#include <esp_random.h>

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
constexpr uint32_t SX1262_REQUEST_BIT = (1 << 3);
constexpr uint32_t SX1262_RESPONSE_BIT = (1 << 0);
// Preamble and sync word detection are latched in the IRQ status for isReceiving(), but only RX done is routed to DIO1
constexpr auto SX1262_IRQ_FLAGS = RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED) | (1UL << RADIOLIB_IRQ_SYNC_WORD_VALID);
constexpr auto SX1262_REQUEST_TIMEOUT_MILLIS = 1000;
constexpr auto SX1262_CAD_TIMEOUT_MILLIS = 500;
constexpr auto SX1262_LBT_MAX_ATTEMPTS = 5;
constexpr auto SX1262_LBT_BACKOFF_MIN_MILLIS = 20;
constexpr auto SX1262_LBT_BACKOFF_MAX_MILLIS = 200;
// RX callbacks run on the radio thread and may do non-trivial work (e.g. packet decryption)
constexpr size_t SX1262_THREAD_STACK_SIZE = 8192;

const char* toString(enum RadioState state) {
    switch (state) {
        case RADIO_STATE_OFF:
            return "off";
        case RADIO_STATE_ON_PENDING:
            return "on-pending";
        case RADIO_STATE_ON:
            return "on";
        case RADIO_STATE_OFF_PENDING:
            return "off-pending";
        case RADIO_STATE_ERROR:
            return "error";
        default:
            return "unknown";
    }
}

const char* toString(enum RadioModulation modulation) {
    switch (modulation) {
        case RADIO_MODULATION_NONE:
            return "none";
        case RADIO_MODULATION_FSK:
            return "FSK";
        case RADIO_MODULATION_LORA:
            return "LoRa";
        case RADIO_MODULATION_LR_FHSS:
            return "LR-FHSS";
        default:
            return "unknown";
    }
}

const char* toString(enum RadioParameter parameter) {
    switch (parameter) {
        case RADIO_PARAMETER_POWER:
            return "power";
        case RADIO_PARAMETER_BOOSTED_GAIN:
            return "boosted gain";
        case RADIO_PARAMETER_FREQUENCY:
            return "frequency";
        case RADIO_PARAMETER_BANDWIDTH:
            return "bandwidth";
        case RADIO_PARAMETER_SPREADING_FACTOR:
            return "spreading factor";
        case RADIO_PARAMETER_CODING_RATE:
            return "coding rate";
        case RADIO_PARAMETER_SYNC_WORD:
            return "sync word";
        case RADIO_PARAMETER_PREAMBLE_LENGTH:
            return "preamble length";
        case RADIO_PARAMETER_FREQUENCY_DEVIATION:
            return "frequency deviation";
        case RADIO_PARAMETER_DATA_RATE:
            return "data rate";
        case RADIO_PARAMETER_NARROW_GRID:
            return "narrow grid";
        case RADIO_PARAMETER_CURRENT_LIMIT:
            return "current limit";
        case RADIO_PARAMETER_LBT:
            return "listen-before-talk";
        case RADIO_PARAMETER_LBT_RSSI_THRESHOLD:
            return "listen-before-talk RSSI threshold";
        default:
            return "unknown";
    }
}

template<typename T>
constexpr error_t checkLimitsAndApply(T& target, const int32_t value, const int32_t lower, const int32_t upper, const int32_t step = 0) {
    if ((value >= lower) && (value <= upper)) {
        if ((step != 0) && ((value % step) != 0)) {
            return ERROR_OUT_OF_RANGE;
        }

        target = static_cast<T>(value);
        return ERROR_NONE;
    }
    return ERROR_OUT_OF_RANGE;
}

template<typename T>
constexpr error_t checkValuesAndApply(T& target, const int32_t value, std::initializer_list<int32_t> valids) {
    for (int32_t valid : valids) {
        if (value == valid) {
            target = static_cast<T>(value);
            return ERROR_NONE;
        }
    }
    return ERROR_OUT_OF_RANGE;
}

/** Exposes the chip's device errors, which explain a failed begin() */
class Sx1262Chip final : public SX1262 {
public:
    using SX1262::SX1262;
    using SX126x::getDeviceErrors;
};

} // namespace

struct Sx1262Radio::RadioParts {
    Sx126xRadiolibHal hal;
    Module radioModule;
    Sx1262Chip radio;

    explicit RadioParts(const Settings& settings)
        : hal(settings.spi_host, settings.spi_frequency_hz, settings.spi_controller)
        , radioModule(&hal, settings.pin_cs, RADIOLIB_NC, settings.pin_reset, settings.pin_busy)
        , radio(&radioModule) {}
};

Sx1262Radio::Sx1262Radio(const Settings& settings)
    : settings(settings) {
    recursive_mutex_construct(&mutex);
    mutex_construct(&requestMutex);
    event_group_construct(&events);
    event_group_construct(&responses);
    parts = tt::newObject<RadioParts>(settings);
    check(parts != nullptr);
}

Sx1262Radio::~Sx1262Radio() {
    setEnabled(false);
    tt::deleteObject(parts);
    event_group_destruct(&responses);
    event_group_destruct(&events);
    mutex_destruct(&requestMutex);
    recursive_mutex_destruct(&mutex);
}

// Datasheet: NRESET must be held low for at least 100 us
constexpr uint32_t RESET_PULSE_MICROS = 200;

error_t Sx1262Radio::probe() const {
    // NRESET output, idle high. BUSY input without pull-up: some modules drive BUSY through a
    // series resistor, which an internal pull-up overpowers.
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
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&busy_conf);

    // Reset pulse (datasheet: NRESET low for >= 100 us triggers a full reset)
    const int busyBeforeReset = gpio_get_level(settings.pin_busy);
    gpio_set_level(settings.pin_reset, 0);
    delay_micros(RESET_PULSE_MICROS);
    const int busyDuringReset = gpio_get_level(settings.pin_busy);
    gpio_set_level(settings.pin_reset, 1);

    // After reset the chip boots and calibrates with BUSY high, then drives BUSY
    // low once it reaches STDBY_RC (datasheet: ~3.5 ms max). Allow a generous
    // margin; a line stuck high means no chip is answering.
    constexpr auto PROBE_TIMEOUT_MILLIS = 20;
    // BUSY level per millisecond after the reset, logged when the probe fails
    char busyTrace[PROBE_TIMEOUT_MILLIS + 2] = {};
    int elapsed = 0;
    while (true) {
        const int busy = gpio_get_level(settings.pin_busy);
        busyTrace[elapsed] = busy ? '1' : '0';
        if (busy == 0) {
            break;
        }
        if (elapsed >= PROBE_TIMEOUT_MILLIS) {
            LOG_E(TAG, "Probe failed: BUSY (GPIO %d) stuck high after reset — module absent or unpowered?", settings.pin_busy);
            LOG_E(TAG, "BUSY before reset %d, during reset %d, after reset per ms: %s", busyBeforeReset, busyDuringReset, busyTrace);
            return ERROR_RESOURCE;
        }
        delay_millis(1);
        elapsed++;
    }

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
    setState(RADIO_STATE_ON);

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

        if (modulation == RADIO_MODULATION_NONE) {
            LOG_E(TAG, "Cannot enable without a modulation set");
            unlock();
            return ERROR_INVALID_STATE;
        }

        if (thread != nullptr) {
            thread_free(thread);
            thread = nullptr;
        }

        threadInterrupted = false;
        setState(RADIO_STATE_ON_PENDING);

        thread = thread_alloc_full("SX1262", SX1262_THREAD_STACK_SIZE, threadMainStatic, this, tskNO_AFFINITY);
        if (thread == nullptr) {
            setState(RADIO_STATE_ERROR);
            unlock();
            return ERROR_OUT_OF_MEMORY;
        }
        thread_set_priority(thread, THREAD_PRIORITY_HIGH);

        if (thread_start(thread) != ERROR_NONE) {
            thread_free(thread);
            thread = nullptr;
            setState(RADIO_STATE_ERROR);
            unlock();
            return ERROR_UNDEFINED;
        }

        unlock();
        return ERROR_NONE;
    } else {
        setState(RADIO_STATE_OFF_PENDING);

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

        setState(RADIO_STATE_OFF);
        unlock();
        return ERROR_NONE;
    }
}

// endregion

// region State, modulation and callbacks

enum RadioState Sx1262Radio::getState() const {
    lock();
    const auto result = state;
    unlock();
    return result;
}

void Sx1262Radio::setState(enum RadioState newState) {
    lock();
    if (state == newState) {
        unlock();
        return;
    }
    LOG_I(TAG, "State: %s -> %s", toString(state), toString(newState));
    state = newState;
    unlock();

    radio_state_event_emit(settings.device, newState);
}

error_t Sx1262Radio::setModulation(enum RadioModulation newModulation) {
    const auto currentState = getState();
    if ((currentState == RADIO_STATE_ON_PENDING) || (currentState == RADIO_STATE_ON)) {
        return ERROR_INVALID_STATE;
    }

    if (!((newModulation == RADIO_MODULATION_NONE) || canTransmit(newModulation) || canReceive(newModulation))) {
        return ERROR_NOT_SUPPORTED;
    }

    lock();
    LOG_I(TAG, "Modulation set to %s", toString(newModulation));
    modulation = newModulation;
    unlock();
    return ERROR_NONE;
}

enum RadioModulation Sx1262Radio::getModulation() const {
    lock();
    const auto result = modulation;
    unlock();
    return result;
}

error_t Sx1262Radio::publishRx(const uint8_t* data, size_t length, float rssi, float snr) {
    return radio_rx_event_emit(settings.device, data, length, rssi, snr);
}

error_t Sx1262Radio::publishTx(RadioTxId id, enum RadioTransmissionState txState) {
    return radio_tx_event_emit(settings.device, id, txState);
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

error_t Sx1262Radio::transmit(const uint8_t* data, size_t length, RadioTxId* id) {
    lock();
    const auto txId = lastTxId;
    lastTxId++;
    txQueue.push_back(TxItem {.id = txId, .data = tt::std::vector<uint8_t>(data, data + length)});
    LOG_D(TAG, "TX id=%d queued: %u bytes (queue depth %u)", (int)txId, (unsigned)length, (unsigned)txQueue.size());
    unlock();

    publishTx(txId, RADIO_TRANSMISSION_STATE_QUEUED);
    event_group_set(events, SX1262_QUEUED_TX_BIT);

    if (id != nullptr) {
        *id = txId;
    }
    return ERROR_NONE;
}

// endregion

// region Parameters

error_t Sx1262Radio::setBaseParameter(enum RadioParameter parameter, int32_t value) {
    switch (parameter) {
        case RADIO_PARAMETER_POWER:
            return checkLimitsAndApply(power, value, -9, 22);
        case RADIO_PARAMETER_BOOSTED_GAIN:
            return checkLimitsAndApply(boostedGain, value, 0, 1, 1);
        case RADIO_PARAMETER_CURRENT_LIMIT:
            // SX1262 OCP range is 0..140 mA (RadioLib clamps to a 2.5 mA step internally).
            return checkLimitsAndApply(currentLimit, value, 0, 140);
        case RADIO_PARAMETER_LBT:
            return checkLimitsAndApply(lbt, value, 0, 1, 1);
        case RADIO_PARAMETER_LBT_RSSI_THRESHOLD:
            return checkLimitsAndApply(lbtRssiThreshold, value, -140, 0);
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

error_t Sx1262Radio::setLoraParameter(enum RadioParameter parameter, int32_t value) {
    switch (parameter) {
        // Frequency in Hz (150..960 MHz)
        case RADIO_PARAMETER_FREQUENCY:
            return checkLimitsAndApply(frequency, value, 150000000, 960000000);
        // Bandwidth in Hz (RadioLib's supported LoRa bandwidths, expressed in Hz)
        case RADIO_PARAMETER_BANDWIDTH:
            return checkValuesAndApply(bandwidth, value, {7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000, 500000});
        case RADIO_PARAMETER_SPREADING_FACTOR:
            return checkLimitsAndApply(spreadingFactor, value, 7, 12, 1);
        case RADIO_PARAMETER_CODING_RATE:
            return checkLimitsAndApply(codingRate, value, 5, 8, 1);
        case RADIO_PARAMETER_SYNC_WORD:
            return checkLimitsAndApply(syncWord, value, 0, 255);
        case RADIO_PARAMETER_PREAMBLE_LENGTH:
            return checkLimitsAndApply(preambleLength, value, 0, 65535);
        default:
            break;
    }

    LOG_W(TAG, "Tried to set unsupported LoRa parameter \"%s\" to %d", toString(parameter), (int)value);
    return ERROR_NOT_SUPPORTED;
}

error_t Sx1262Radio::setFskParameter(enum RadioParameter parameter, int32_t value) {
    switch (parameter) {
        // Frequency in Hz (150..960 MHz)
        case RADIO_PARAMETER_FREQUENCY:
            return checkLimitsAndApply(frequency, value, 150000000, 960000000);
        // RX bandwidth in Hz (RadioLib's supported FSK bandwidths, expressed in Hz)
        case RADIO_PARAMETER_BANDWIDTH:
            return checkValuesAndApply(bandwidth, value, {4800, 5800, 7300, 9700, 11700, 14600, 19500, 23400, 29300, 39000, 46900, 58600, 78200});
        case RADIO_PARAMETER_PREAMBLE_LENGTH:
            return checkLimitsAndApply(preambleLength, value, 0, 65535);
        // Bit rate in bit/s (0.6..300 kbps)
        case RADIO_PARAMETER_DATA_RATE:
            return checkLimitsAndApply(bitRate, value, 600, 300000);
        // Frequency deviation in Hz (0..200 kHz)
        case RADIO_PARAMETER_FREQUENCY_DEVIATION:
            return checkLimitsAndApply(frequencyDeviation, value, 0, 200000);
        default:
            break;
    }

    LOG_W(TAG, "Tried to set unsupported FSK parameter \"%s\" to %d", toString(parameter), (int)value);
    return ERROR_NOT_SUPPORTED;
}

error_t Sx1262Radio::setLrFhssParameter(enum RadioParameter parameter, int32_t value) {
    switch (parameter) {
        // Bandwidth in Hz (RadioLib's supported LR-FHSS bandwidths, expressed in Hz)
        case RADIO_PARAMETER_BANDWIDTH:
            return checkValuesAndApply(bandwidth, value, {39060, 85940, 136720, 183590, 335940, 386720, 722660, 773440, 1523400, 1574200});
        case RADIO_PARAMETER_CODING_RATE:
            return checkValuesAndApply(codingRate, value, {RADIOLIB_SX126X_LR_FHSS_CR_5_6, RADIOLIB_SX126X_LR_FHSS_CR_2_3, RADIOLIB_SX126X_LR_FHSS_CR_1_2, RADIOLIB_SX126X_LR_FHSS_CR_1_3});
        case RADIO_PARAMETER_NARROW_GRID:
            return checkLimitsAndApply(narrowGrid, value, 0, 1, 1);
        default:
            break;
    }

    LOG_W(TAG, "Tried to set unsupported LR-FHSS parameter \"%s\" to %d", toString(parameter), (int)value);
    return ERROR_NOT_SUPPORTED;
}

error_t Sx1262Radio::setParameter(enum RadioParameter parameter, int32_t value) {
    lock();

    error_t result = setBaseParameter(parameter, value);
    if (result == ERROR_NOT_SUPPORTED) {
        switch (modulation) {
            case RADIO_MODULATION_LORA:
                result = setLoraParameter(parameter, value);
                break;
            case RADIO_MODULATION_FSK:
                result = setFskParameter(parameter, value);
                break;
            case RADIO_MODULATION_LR_FHSS:
                result = setLrFhssParameter(parameter, value);
                break;
            default:
                break;
        }
    }

    if (result == ERROR_NONE) {
        LOG_D(TAG, "Parameter %s = %d", toString(parameter), (int)value);
    }

    unlock();
    return result;
}

error_t Sx1262Radio::getBaseParameter(enum RadioParameter parameter, int32_t* value) const {
    switch (parameter) {
        case RADIO_PARAMETER_POWER:
            *value = power;
            return ERROR_NONE;
        case RADIO_PARAMETER_BOOSTED_GAIN:
            *value = boostedGain;
            return ERROR_NONE;
        case RADIO_PARAMETER_CURRENT_LIMIT:
            *value = currentLimit;
            return ERROR_NONE;
        case RADIO_PARAMETER_LBT:
            *value = lbt;
            return ERROR_NONE;
        case RADIO_PARAMETER_LBT_RSSI_THRESHOLD:
            *value = lbtRssiThreshold;
            return ERROR_NONE;
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

error_t Sx1262Radio::getLoraParameter(enum RadioParameter parameter, int32_t* value) const {
    switch (parameter) {
        case RADIO_PARAMETER_FREQUENCY:
            *value = frequency;
            return ERROR_NONE;
        case RADIO_PARAMETER_BANDWIDTH:
            *value = bandwidth;
            return ERROR_NONE;
        case RADIO_PARAMETER_SPREADING_FACTOR:
            *value = spreadingFactor;
            return ERROR_NONE;
        case RADIO_PARAMETER_CODING_RATE:
            *value = codingRate;
            return ERROR_NONE;
        case RADIO_PARAMETER_SYNC_WORD:
            *value = syncWord;
            return ERROR_NONE;
        case RADIO_PARAMETER_PREAMBLE_LENGTH:
            *value = preambleLength;
            return ERROR_NONE;
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

error_t Sx1262Radio::getFskParameter(enum RadioParameter parameter, int32_t* value) const {
    switch (parameter) {
        case RADIO_PARAMETER_FREQUENCY:
            *value = frequency;
            return ERROR_NONE;
        case RADIO_PARAMETER_BANDWIDTH:
            *value = bandwidth;
            return ERROR_NONE;
        case RADIO_PARAMETER_DATA_RATE:
            *value = bitRate;
            return ERROR_NONE;
        case RADIO_PARAMETER_FREQUENCY_DEVIATION:
            *value = frequencyDeviation;
            return ERROR_NONE;
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

error_t Sx1262Radio::getLrFhssParameter(enum RadioParameter parameter, int32_t* value) const {
    switch (parameter) {
        case RADIO_PARAMETER_BANDWIDTH:
            *value = bandwidth;
            return ERROR_NONE;
        case RADIO_PARAMETER_CODING_RATE:
            *value = codingRate;
            return ERROR_NONE;
        case RADIO_PARAMETER_NARROW_GRID:
            *value = narrowGrid;
            return ERROR_NONE;
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

error_t Sx1262Radio::getParameter(enum RadioParameter parameter, int32_t* value) const {
    lock();

    // No warnings are emitted to be able to discover parameters by return status
    error_t result = getBaseParameter(parameter, value);
    if (result == ERROR_NOT_SUPPORTED) {
        switch (modulation) {
            case RADIO_MODULATION_LORA:
                result = getLoraParameter(parameter, value);
                break;
            case RADIO_MODULATION_FSK:
                result = getFskParameter(parameter, value);
                break;
            case RADIO_MODULATION_LR_FHSS:
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

int Sx1262Radio::doBegin(enum RadioModulation beginModulation) {
    int16_t rc = RADIOLIB_ERR_NONE;
    auto& radio = parts->radio;

    // RadioLib takes MHz/kHz/kbps floats; the driver stores Hz/bit/s integers.
    const float frequencyMhz = static_cast<float>(frequency) / 1000000.0f;
    const float bandwidthKhz = static_cast<float>(bandwidth) / 1000.0f;

    if (beginModulation == RADIO_MODULATION_LORA) {
        LOG_I(
            TAG,
            "Starting LoRa: %.3f MHz, BW %.2f kHz, SF%u, CR 4/%u, sync 0x%02X, preamble %u, %d dBm, TCXO %.1f V",
            frequencyMhz,
            bandwidthKhz,
            spreadingFactor,
            codingRate,
            syncWord,
            preambleLength,
            power,
            settings.tcxo_voltage
        );
        rc = radio.begin(
            frequencyMhz,
            bandwidthKhz,
            spreadingFactor,
            codingRate,
            syncWord,
            power,
            preambleLength,
            settings.tcxo_voltage,
            settings.use_regulator_ldo
        );
    } else if (beginModulation == RADIO_MODULATION_FSK) {
        const float bitRateKbps = static_cast<float>(bitRate) / 1000.0f;
        const float frequencyDeviationKhz = static_cast<float>(frequencyDeviation) / 1000.0f;
        LOG_I(
            TAG,
            "Starting FSK: %.3f MHz, %.2f kbps, deviation %.1f kHz, BW %.1f kHz, preamble %u, %d dBm",
            frequencyMhz,
            bitRateKbps,
            frequencyDeviationKhz,
            bandwidthKhz,
            preambleLength,
            power
        );
        rc = radio.beginFSK(
            frequencyMhz,
            bitRateKbps,
            frequencyDeviationKhz,
            bandwidthKhz,
            power,
            preambleLength,
            settings.tcxo_voltage,
            settings.use_regulator_ldo
        );
    } else if (beginModulation == RADIO_MODULATION_LR_FHSS) {
        // NOTE: LR-FHSS is unvalidated. RadioLib's beginLRFHSS() takes
        // (freq, bw-index, cr, narrowGrid, ...) where bw is a RADIOLIB_SX126X_LR_FHSS_BW_*
        // index, not a frequency; this call passes the stored bandwidth into the freq slot
        // and is known to be incomplete. Left as-is pending a dedicated LR-FHSS bring-up —
        // the LoRa and FSK paths above are the hardware-validated ones.
        LOG_I(TAG, "Starting LR-FHSS: BW %d Hz, CR %u, %s grid", (int)bandwidth, codingRate, narrowGrid ? "narrow" : "wide");
        rc = radio.beginLRFHSS(
            bandwidth,
            codingRate,
            narrowGrid,
            settings.tcxo_voltage,
            settings.use_regulator_ldo
        );
    } else {
        LOG_E(TAG, "SX1262 not capable of modulation \"%s\"", toString(beginModulation));
        setState(RADIO_STATE_ERROR);
        return -1;
    }

    if (rc != RADIOLIB_ERR_NONE) {
        // Device error bits: 0 RC64K, 1 RC13M, 2 PLL, 3 ADC, 4 image calibration, 5 XOSC start, 6 PLL lock, 8 PA ramp
        LOG_E(TAG, "RadioLib initialization failed with code %hi, device errors 0x%04X", rc, (unsigned)radio.getDeviceErrors());
        setState(RADIO_STATE_ERROR);
        return -1;
    }

    // Apply the PA over-current protection limit. RadioLib's begin() already set its
    // fail-safe default (60 mA), so this is only meaningful when a consumer raised it
    // via RADIO_PARAMETER_CURRENT_LIMIT to reach higher output power.
    rc = radio.setCurrentLimit(static_cast<float>(currentLimit));
    if (rc != RADIOLIB_ERR_NONE) {
        LOG_E(TAG, "Setting current limit to %d mA failed with code %hi", (int)currentLimit, rc);
        setState(RADIO_STATE_ERROR);
        return -1;
    }

    // Modules that wire the antenna TX/RX switch to DIO2 (e.g. LilyGO T-Deck Max)
    // must enable this or the RF path stays disconnected and no TX/RX gets through.
    if (settings.dio2_rf_switch) {
        rc = radio.setDio2AsRfSwitch(true);
        if (rc != RADIOLIB_ERR_NONE) {
            LOG_E(TAG, "Setting DIO2 as RF switch failed with code %hi", rc);
            setState(RADIO_STATE_ERROR);
            return -1;
        }
    }

    if (settings.pin_rx_enable != GPIO_NUM_NC) {
        radio.setRfSwitchPins(settings.pin_rx_enable, RADIOLIB_NC);
    }

    rc = radio.setRxBoostedGainMode(boostedGain, true);
    if (rc != RADIOLIB_ERR_NONE) {
        LOG_E(TAG, "Setting RX boosted gain to %s failed with code %hi", boostedGain ? "true" : "false", rc);
        setState(RADIO_STATE_ERROR);
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

    if (!waitForClearChannel()) {
        if (isThreadInterrupted()) {
            publishTx(currentTx.id, RADIO_TRANSMISSION_STATE_ERROR);
        } else {
            LOG_W(TAG, "TX id=%d: channel busy", (int)currentTx.id);
            publishTx(currentTx.id, RADIO_TRANSMISSION_STATE_CHANNEL_BUSY);
        }
        return;
    }

    LOG_I(TAG, "TX id=%d: %u bytes (%u more queued)", (int)currentTx.id, (unsigned)currentTx.data.size(), (unsigned)getTxQueueSize());
    rc = radio.startTransmit(currentTx.data.data(), currentTx.data.size());

    if (rc == RADIOLIB_ERR_NONE) {
        publishTx(currentTx.id, RADIO_TRANSMISSION_STATE_TRANSMIT_PENDING);

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
            publishTx(currentTx.id, RADIO_TRANSMISSION_STATE_ERROR);
            return;
        }

        // If the DIO1 bit is unset, this means the wait timed out
        if (txEventFlags & SX1262_DIO1_EVENT_BIT) {
            LOG_I(TAG, "TX id=%d: done", (int)currentTx.id);
            publishTx(currentTx.id, RADIO_TRANSMISSION_STATE_TRANSMITTED);
        } else {
            LOG_W(TAG, "TX id=%d: no TX-done IRQ within %u ms", (int)currentTx.id, (unsigned)txTimeoutMillis);
            publishTx(currentTx.id, RADIO_TRANSMISSION_STATE_TIMEOUT);
        }
    } else {
        LOG_E(TAG, "Error transmitting id=%d, rc=%hi", (int)currentTx.id, rc);
        publishTx(currentTx.id, RADIO_TRANSMISSION_STATE_ERROR);
    }
}

bool Sx1262Radio::doListen() {
    auto& radio = parts->radio;

    if (getModulation() != RADIO_MODULATION_LR_FHSS) {
        int16_t rc = radio.startReceiveDutyCycleAuto(preambleLength, 0, SX1262_IRQ_FLAGS);
        if (rc == RADIOLIB_ERR_NONE) {
            while (true) {
                uint32_t flags = 0;
                event_group_wait(
                    events,
                    SX1262_INTERRUPT_BIT | SX1262_DIO1_EVENT_BIT | SX1262_QUEUED_TX_BIT | SX1262_REQUEST_BIT,
                    false,
                    false,
                    &flags,
                    portMAX_DELAY
                );
                // Requests are served without leaving RX, so a packet in flight isn't lost
                if ((flags & (SX1262_INTERRUPT_BIT | SX1262_DIO1_EVENT_BIT | SX1262_QUEUED_TX_BIT)) == 0) {
                    event_group_clear(events, SX1262_REQUEST_BIT);
                    handleRequest();
                    continue;
                }
                event_group_clear(events, flags & (SX1262_INTERRUPT_BIT | SX1262_DIO1_EVENT_BIT | SX1262_QUEUED_TX_BIT));
                // A packet that arrived after the wake is received before a transmission can discard it
                if ((event_group_get(events) & SX1262_DIO1_EVENT_BIT) != 0) {
                    event_group_clear(events, SX1262_DIO1_EVENT_BIT);
                    flags |= SX1262_DIO1_EVENT_BIT;
                }
                return (flags & SX1262_DIO1_EVENT_BIT) != 0;
            }
        } else {
            LOG_E(TAG, "Error setting dutycycle RX, RadioLib returned %hi", rc);
        }
        return false;
    } else {
        // LR-FHSS modem only supports TX
        while (true) {
            uint32_t flags = 0;
            event_group_wait(
                events,
                SX1262_INTERRUPT_BIT | SX1262_QUEUED_TX_BIT | SX1262_REQUEST_BIT,
                false,
                false,
                &flags,
                portMAX_DELAY
            );
            if ((flags & (SX1262_INTERRUPT_BIT | SX1262_QUEUED_TX_BIT)) == 0) {
                event_group_clear(events, SX1262_REQUEST_BIT);
                handleRequest();
                continue;
            }
            event_group_clear(events, SX1262_INTERRUPT_BIT | SX1262_QUEUED_TX_BIT);
            return false;
        }
    }
}

void Sx1262Radio::doReceive() {
    // LR-FHSS modem only supports TX
    if (getModulation() == RADIO_MODULATION_LR_FHSS) return;

    auto& radio = parts->radio;

    uint16_t rxSize = radio.getPacketLength(true);
    tt::std::vector<uint8_t> data(rxSize);
    int16_t rc = radio.readData(data.data(), rxSize);
    if (rc != RADIOLIB_ERR_NONE) {
        LOG_E(TAG, "Error receiving data, RadioLib returned %hi", rc);
    } else if (rxSize == 0) {
        // Empty read: skip silently to avoid log flooding on spurious IRQs.
    } else {
        const float rssi = radio.getRSSI();
        const float snr = radio.getSNR();
        LOG_I(TAG, "RX: %u bytes, RSSI %.1f dBm, SNR %.1f dB", (unsigned)data.size(), rssi, snr);
        publishRx(data.data(), data.size(), rssi, snr);
        radio.finishReceive();
    }
}

bool Sx1262Radio::isChannelBusy() {
    auto& radio = parts->radio;

    if (getModulation() == RADIO_MODULATION_LORA) {
        // Channel activity detection reports through DIO1
        event_group_clear(events, SX1262_DIO1_EVENT_BIT);
        gpio_descriptor_enable_interrupt(settings.dio1);
        int16_t rc = radio.startChannelScan();
        if (rc != RADIOLIB_ERR_NONE) {
            LOG_W(TAG, "Channel scan failed to start, RadioLib returned %hi", rc);
            return false;
        }
        uint32_t flags = 0;
        event_group_wait(events, SX1262_DIO1_EVENT_BIT, false, true, &flags, pdMS_TO_TICKS(SX1262_CAD_TIMEOUT_MILLIS));
        const bool busy = radio.getChannelScanResult() == RADIOLIB_LORA_DETECTED;
        radio.clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
        radio.standby();
        // Re-arm for the TX-done wait, as the ISR masks the interrupt when it fires
        event_group_clear(events, SX1262_DIO1_EVENT_BIT);
        gpio_descriptor_enable_interrupt(settings.dio1);
        return busy;
    }

    // FSK: sample the RSSI in continuous RX
    if (radio.startReceive() != RADIOLIB_ERR_NONE) {
        return false;
    }
    delay_millis(2);
    const float rssi = radio.getRSSI(false);
    radio.standby();
    radio.clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
    event_group_clear(events, SX1262_DIO1_EVENT_BIT);
    gpio_descriptor_enable_interrupt(settings.dio1);
    return rssi >= static_cast<float>(lbtRssiThreshold);
}

bool Sx1262Radio::waitForClearChannel() {
    lock();
    const bool enabled = lbt && (modulation != RADIO_MODULATION_LR_FHSS);
    unlock();
    if (!enabled) {
        return true;
    }

    for (int attempt = 0; attempt < SX1262_LBT_MAX_ATTEMPTS; attempt++) {
        if (!isChannelBusy()) {
            return true;
        }
        const uint32_t backoffMillis = SX1262_LBT_BACKOFF_MIN_MILLIS + (esp_random() % (SX1262_LBT_BACKOFF_MAX_MILLIS - SX1262_LBT_BACKOFF_MIN_MILLIS));
        LOG_D(TAG, "Channel busy, backing off %u ms", (unsigned)backoffMillis);
        // Returns early when the radio is being turned off
        if (event_group_wait(events, SX1262_INTERRUPT_BIT, false, false, nullptr, pdMS_TO_TICKS(backoffMillis)) == ERROR_NONE) {
            return false;
        }
    }
    return false;
}

void Sx1262Radio::handleRequest() {
    auto& radio = parts->radio;
    const auto request = pendingRequest;
    requestResult = ERROR_NONE;

    if (request == Request::Present) {
        char version[16] = {};
        parts->radioModule.SPIreadRegisterBurst(RADIOLIB_SX126X_REG_VERSION_STRING, sizeof(version), reinterpret_cast<uint8_t*>(version));
        requestResult = (strncmp(version, "SX126", 5) == 0) ? ERROR_NONE : ERROR_NOT_FOUND;
    } else if (getModulation() == RADIO_MODULATION_LR_FHSS) {
        requestResult = ERROR_NOT_SUPPORTED;
    } else if (request == Request::Rssi) {
        requestRssi = radio.getRSSI(false);
    } else if (request == Request::Receiving) {
        const uint32_t irq = radio.getIrqFlags();
        const uint32_t detected = RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED | RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID | RADIOLIB_SX126X_IRQ_HEADER_VALID;
        const uint32_t ended = RADIOLIB_SX126X_IRQ_RX_DONE | RADIOLIB_SX126X_IRQ_HEADER_ERR | RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_TIMEOUT;
        requestReceiving = ((irq & detected) != 0) && ((irq & ended) == 0);
        // A reception that failed without RX done leaves its detection flags latched
        if ((irq & ended) != 0 && (irq & RADIOLIB_SX126X_IRQ_RX_DONE) == 0) {
            radio.clearIrqFlags(detected | ended);
        }
    }

    event_group_set(responses, SX1262_RESPONSE_BIT);
}

error_t Sx1262Radio::performRequest(Request request) {
    if (getState() != RADIO_STATE_ON) {
        return ERROR_INVALID_STATE;
    }

    mutex_lock(&requestMutex);
    pendingRequest = request;
    event_group_clear(responses, SX1262_RESPONSE_BIT);
    event_group_set(events, SX1262_REQUEST_BIT);
    const error_t waitResult = event_group_wait(responses, SX1262_RESPONSE_BIT, false, true, nullptr, pdMS_TO_TICKS(SX1262_REQUEST_TIMEOUT_MILLIS));
    const error_t result = (waitResult == ERROR_NONE) ? requestResult : ERROR_TIMEOUT;
    mutex_unlock(&requestMutex);
    return result;
}

error_t Sx1262Radio::getRssi(float* rssi) {
    const error_t result = performRequest(Request::Rssi);
    if (result == ERROR_NONE) {
        *rssi = requestRssi;
    }
    return result;
}

error_t Sx1262Radio::checkPresent() {
    if (getState() != RADIO_STATE_ON) {
        return ERROR_NONE;
    }
    // A busy or changing radio counts as present
    return (performRequest(Request::Present) == ERROR_NOT_FOUND) ? ERROR_NOT_FOUND : ERROR_NONE;
}

error_t Sx1262Radio::isReceiving(bool* receiving) {
    const error_t result = performRequest(Request::Receiving);
    if (result == ERROR_NONE) {
        *receiving = requestReceiving;
    }
    return result;
}

// endregion
