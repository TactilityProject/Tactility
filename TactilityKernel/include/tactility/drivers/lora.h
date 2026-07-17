// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;

/**
 * Device type and API for sub-GHz packet transceivers such as the Semtech SX126x family.
 * The API is LoRa-centric but exposes the modem's other modulation schemes as well.
 */

enum LoraModulation {
    LORA_MODULATION_NONE = 0,
    LORA_MODULATION_FSK,
    LORA_MODULATION_LORA,
    LORA_MODULATION_LR_FHSS,
};

/**
 * Tunable radio parameters. Values are passed as float; the unit per parameter:
 * - POWER: TX output power in dBm
 * - BOOSTED_GAIN: boosted RX gain mode, 0 or 1
 * - FREQUENCY: carrier frequency in MHz
 * - BANDWIDTH: bandwidth in kHz
 * - SPREADING_FACTOR: LoRa spreading factor (7-12)
 * - CODING_RATE: LoRa coding rate denominator (5-8 for 4/5-4/8)
 * - SYNC_WORD: sync word value
 * - PREAMBLE_LENGTH: preamble length in symbols (LoRa) or bits (FSK)
 * - FREQUENCY_DEVIATION: FSK frequency deviation in kHz
 * - DATA_RATE: FSK bit rate in kbps
 * - NARROW_GRID: LR-FHSS grid spacing, 0 (25 kHz) or 1 (3.9 kHz)
 * - CURRENT_LIMIT: PA over-current protection limit in mA. Fail-safe: a low limit caps
 *   the current the PA can push into a bad or disconnected antenna, but also caps the
 *   achievable output power. Drivers keep a conservative default; a board-aware consumer
 *   that knows its antenna and PA can raise it to reach full output power.
 *
 * Which parameters are available depends on the driver and the selected modulation.
 */
enum LoraParameter {
    LORA_PARAMETER_POWER = 0,
    LORA_PARAMETER_BOOSTED_GAIN,
    LORA_PARAMETER_FREQUENCY,
    LORA_PARAMETER_BANDWIDTH,
    LORA_PARAMETER_SPREADING_FACTOR,
    LORA_PARAMETER_CODING_RATE,
    LORA_PARAMETER_SYNC_WORD,
    LORA_PARAMETER_PREAMBLE_LENGTH,
    LORA_PARAMETER_FREQUENCY_DEVIATION,
    LORA_PARAMETER_DATA_RATE,
    LORA_PARAMETER_NARROW_GRID,
    LORA_PARAMETER_CURRENT_LIMIT,
};

enum LoraRadioState {
    LORA_RADIO_STATE_OFF,
    LORA_RADIO_STATE_ON_PENDING,
    LORA_RADIO_STATE_ON,
    LORA_RADIO_STATE_OFF_PENDING,
    LORA_RADIO_STATE_ERROR,
};

/** Identifies a queued transmission. Unique per device until it wraps around. */
typedef int32_t LoraTxId;

enum LoraTransmissionState {
    /** Accepted into the TX queue */
    LORA_TRANSMISSION_STATE_QUEUED,
    /** Handed to the modem, waiting for TX-done */
    LORA_TRANSMISSION_STATE_TRANSMIT_PENDING,
    /** TX-done confirmed by the modem */
    LORA_TRANSMISSION_STATE_TRANSMITTED,
    /** No TX-done within the driver's timeout */
    LORA_TRANSMISSION_STATE_TIMEOUT,
    /** The modem rejected the transmission */
    LORA_TRANSMISSION_STATE_ERROR,
};

struct LoraRxPacket {
    /** Packet payload. Only valid for the duration of the RX callback. */
    const uint8_t* data;
    size_t length;
    /** Received signal strength in dBm */
    float rssi;
    /** Signal-to-noise ratio in dB */
    float snr;
};

/**
 * Callbacks are invoked without any driver lock held, either from the driver's radio
 * thread (RX, TX progress, state) or from the thread calling transmit()/set_enabled()
 * (the QUEUED TX event and enable/disable state changes). Taking consumer locks in a
 * callback is therefore safe, but keep callbacks short: RX processing stalls while
 * they run. After remove_*_callback returns, a callback that was already in flight
 * may still complete once — disable the radio before destroying callback context.
 */
typedef void (*LoraStateCallback)(struct Device* device, void* context, enum LoraRadioState state);
typedef void (*LoraRxCallback)(struct Device* device, void* context, const struct LoraRxPacket* packet);
typedef void (*LoraTxCallback)(struct Device* device, void* context, LoraTxId id, enum LoraTransmissionState state);

struct LoraApi {
    /**
     * Get the radio state of the device.
     * @param[in] device the lora device
     * @param[out] state the radio state
     * @return ERROR_NONE on success
     */
    error_t (*get_radio_state)(struct Device* device, enum LoraRadioState* state);

    /**
     * Turn the radio on or off. Requires a modulation to be set before enabling.
     * Turning on is asynchronous: observe the radio state to know when it's up.
     * @param[in] device the lora device
     * @param[in] enabled true to turn the radio on
     * @return ERROR_NONE on success
     * @retval ERROR_INVALID_STATE when enabling without a modulation set
     */
    error_t (*set_enabled)(struct Device* device, bool enabled);

    /**
     * Set the modulation scheme. Only allowed while the radio is off.
     * @param[in] device the lora device
     * @param[in] modulation the modulation scheme
     * @return ERROR_NONE on success
     * @retval ERROR_INVALID_STATE when the radio is on or turning on
     * @retval ERROR_NOT_SUPPORTED when the device supports neither TX nor RX for this modulation
     */
    error_t (*set_modulation)(struct Device* device, enum LoraModulation modulation);

    /**
     * Get the current modulation scheme.
     * @param[in] device the lora device
     * @param[out] modulation the modulation scheme
     * @return ERROR_NONE on success
     */
    error_t (*get_modulation)(struct Device* device, enum LoraModulation* modulation);

    /**
     * @param[in] device the lora device
     * @param[in] modulation the modulation scheme
     * @return true when the device can transmit with the given modulation
     */
    bool (*can_transmit)(struct Device* device, enum LoraModulation modulation);

    /**
     * @param[in] device the lora device
     * @param[in] modulation the modulation scheme
     * @return true when the device can receive with the given modulation
     */
    bool (*can_receive)(struct Device* device, enum LoraModulation modulation);

    /**
     * Set a radio parameter. See LoraParameter for units.
     * Parameters apply to the current modulation and take effect the next time the radio turns on.
     * @param[in] device the lora device
     * @param[in] parameter the parameter to set
     * @param[in] value the value to set
     * @return ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED when the parameter doesn't apply to the device or modulation
     * @retval ERROR_OUT_OF_RANGE when the value is invalid for the parameter
     */
    error_t (*set_parameter)(struct Device* device, enum LoraParameter parameter, float value);

    /**
     * Get a radio parameter. See LoraParameter for units.
     * @param[in] device the lora device
     * @param[in] parameter the parameter to get
     * @param[out] value the current value
     * @return ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED when the parameter doesn't apply to the device or modulation
     */
    error_t (*get_parameter)(struct Device* device, enum LoraParameter parameter, float* value);

    /**
     * Queue a packet for transmission. The data is copied.
     * Progress is reported through the TX callbacks, starting with QUEUED.
     * @param[in] device the lora device
     * @param[in] data the packet payload
     * @param[in] length the payload length in bytes
     * @param[out] id the id assigned to this transmission (optional, can be NULL)
     * @return ERROR_NONE on success
     */
    error_t (*transmit)(struct Device* device, const uint8_t* data, size_t length, LoraTxId* id);

    /**
     * Add a callback for received packets.
     * @param[in] device the lora device
     * @param[in] callback_context the context to pass to the callback
     * @param[in] callback the callback function
     * @return ERROR_NONE on success
     */
    error_t (*add_rx_callback)(struct Device* device, void* callback_context, LoraRxCallback callback);

    /**
     * Remove a callback for received packets.
     * @param[in] device the lora device
     * @param[in] callback the callback function
     * @return ERROR_NONE on success
     */
    error_t (*remove_rx_callback)(struct Device* device, LoraRxCallback callback);

    /**
     * Add a callback for radio state changes.
     * @param[in] device the lora device
     * @param[in] callback_context the context to pass to the callback
     * @param[in] callback the callback function
     * @return ERROR_NONE on success
     */
    error_t (*add_state_callback)(struct Device* device, void* callback_context, LoraStateCallback callback);

    /**
     * Remove a callback for radio state changes.
     * @param[in] device the lora device
     * @param[in] callback the callback function
     * @return ERROR_NONE on success
     */
    error_t (*remove_state_callback)(struct Device* device, LoraStateCallback callback);

    /**
     * Add a callback for transmission progress.
     * @param[in] device the lora device
     * @param[in] callback_context the context to pass to the callback
     * @param[in] callback the callback function
     * @return ERROR_NONE on success
     */
    error_t (*add_tx_callback)(struct Device* device, void* callback_context, LoraTxCallback callback);

    /**
     * Remove a callback for transmission progress.
     * @param[in] device the lora device
     * @param[in] callback the callback function
     * @return ERROR_NONE on success
     */
    error_t (*remove_tx_callback)(struct Device* device, LoraTxCallback callback);
};

extern const struct DeviceType LORA_TYPE;

/** @return the first registered lora device, regardless of started state, or NULL if none exists */
struct Device* lora_find_first_registered_device(void);

error_t lora_get_radio_state(struct Device* device, enum LoraRadioState* state);
error_t lora_set_enabled(struct Device* device, bool enabled);
error_t lora_set_modulation(struct Device* device, enum LoraModulation modulation);
error_t lora_get_modulation(struct Device* device, enum LoraModulation* modulation);
bool lora_can_transmit(struct Device* device, enum LoraModulation modulation);
bool lora_can_receive(struct Device* device, enum LoraModulation modulation);
error_t lora_set_parameter(struct Device* device, enum LoraParameter parameter, float value);
error_t lora_get_parameter(struct Device* device, enum LoraParameter parameter, float* value);
error_t lora_transmit(struct Device* device, const uint8_t* data, size_t length, LoraTxId* id);
error_t lora_add_rx_callback(struct Device* device, void* callback_context, LoraRxCallback callback);
error_t lora_remove_rx_callback(struct Device* device, LoraRxCallback callback);
error_t lora_add_state_callback(struct Device* device, void* callback_context, LoraStateCallback callback);
error_t lora_remove_state_callback(struct Device* device, LoraStateCallback callback);
error_t lora_add_tx_callback(struct Device* device, void* callback_context, LoraTxCallback callback);
error_t lora_remove_tx_callback(struct Device* device, LoraTxCallback callback);

#ifdef __cplusplus
}
#endif
