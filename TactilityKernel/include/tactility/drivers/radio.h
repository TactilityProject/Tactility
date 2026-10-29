// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;

/**
 * Device type and API for packet radio transceivers such as the Semtech SX126x family
 * and the TI CC1101. Drivers report the modulations they support via can_transmit()
 * and can_receive().
 */

enum RadioModulation {
    RADIO_MODULATION_NONE = 0,
    /** 2-FSK or GFSK */
    RADIO_MODULATION_FSK,
    RADIO_MODULATION_LORA,
    RADIO_MODULATION_LR_FHSS,
    RADIO_MODULATION_FSK4,
    RADIO_MODULATION_MSK,
    /** On-off keying (ASK) */
    RADIO_MODULATION_OOK,
    /**
     * ESP-NOW over Wi-Fi in the 2.4 GHz band. Select the Wi-Fi channel with RADIO_PARAMETER_CHANNEL.
     * The Wi-Fi stack applies the transmit power limits of the country, so regions don't restrict it.
     */
    RADIO_MODULATION_ESPNOW,
};

/**
 * Tunable radio parameters. Values are int32_t; the unit is documented per parameter.
 * Frequencies and rates use their base SI unit (Hz, bit/s) on purpose: a float can't hold
 * a value like 906875000 Hz (906.875 MHz) exactly, so integers avoid the rounding a
 * fractional MHz/kHz representation would introduce.
 *
 * Which parameters are available depends on the driver and the selected modulation.
 */
enum RadioParameter {
    /** TX output power in dBm */
    RADIO_PARAMETER_POWER = 0,
    /** Boosted RX gain mode: 0 = off, 1 = on */
    RADIO_PARAMETER_BOOSTED_GAIN,
    /** Carrier frequency in Hz */
    RADIO_PARAMETER_FREQUENCY,
    /** Bandwidth in Hz */
    RADIO_PARAMETER_BANDWIDTH,
    /** LoRa spreading factor (7-12) */
    RADIO_PARAMETER_SPREADING_FACTOR,
    /** LoRa coding rate denominator (5-8 for 4/5 to 4/8) */
    RADIO_PARAMETER_CODING_RATE,
    /**
     * Sync word. Distinguishes otherwise-identical networks: a receiver only accepts
     * packets whose sync word matches.
     * LoRa: 0x00-0xFF, conventionally 0x12 for private networks and 0x34 for public/LoRaWAN.
     * Other modulations: driver-defined width of up to 32 bits.
     */
    RADIO_PARAMETER_SYNC_WORD,
    /** Preamble length in symbols (LoRa) or bits (other modulations) */
    RADIO_PARAMETER_PREAMBLE_LENGTH,
    /** Frequency deviation from the carrier in Hz (FSK, FSK4, MSK) */
    RADIO_PARAMETER_FREQUENCY_DEVIATION,
    /** Bit rate in bits per second (FSK, FSK4, MSK, OOK) */
    RADIO_PARAMETER_DATA_RATE,
    /** LR-FHSS grid spacing: 0 = 25 kHz (wide), 1 = 3.9 kHz (narrow) */
    RADIO_PARAMETER_NARROW_GRID,
    /**
     * PA over-current protection limit in mA. Fail-safe: a low limit caps the current the
     * PA can push into a bad or disconnected antenna, but also caps the achievable output
     * power. Drivers keep a conservative default; a board-aware consumer that knows its
     * antenna and PA can raise it to reach full output power.
     */
    RADIO_PARAMETER_CURRENT_LIMIT,
    /**
     * Listen-before-talk: 0 = off, 1 = on. When on, the driver checks the channel before each
     * transmission (channel activity detection for LoRa, RSSI threshold otherwise), backs off
     * randomly while it is busy and reports RADIO_TRANSMISSION_STATE_CHANNEL_BUSY when it stays busy.
     */
    RADIO_PARAMETER_LBT,
    /** Listen-before-talk RSSI threshold in dBm: the channel counts as busy at or above it */
    RADIO_PARAMETER_LBT_RSSI_THRESHOLD,
    /** Channel number, for radios that are tuned by channel rather than by frequency (ESP-NOW: Wi-Fi channel 1-14) */
    RADIO_PARAMETER_CHANNEL,
};

enum RadioState {
    RADIO_STATE_OFF,
    RADIO_STATE_ON_PENDING,
    RADIO_STATE_ON,
    RADIO_STATE_OFF_PENDING,
    RADIO_STATE_ERROR,
};

/** Identifies a queued transmission. Unique per device until it wraps around. */
typedef int32_t RadioTxId;

enum RadioTransmissionState {
    /** Accepted into the TX queue */
    RADIO_TRANSMISSION_STATE_QUEUED,
    /** Handed to the modem, waiting for TX-done */
    RADIO_TRANSMISSION_STATE_TRANSMIT_PENDING,
    /** TX-done confirmed by the modem */
    RADIO_TRANSMISSION_STATE_TRANSMITTED,
    /** No TX-done within the driver's timeout */
    RADIO_TRANSMISSION_STATE_TIMEOUT,
    /** The modem rejected the transmission */
    RADIO_TRANSMISSION_STATE_ERROR,
    /** Listen-before-talk found the channel busy on every attempt */
    RADIO_TRANSMISSION_STATE_CHANNEL_BUSY,
};

/**
 * Regulatory region. Each region allows one or more frequency bands with a maximum TX power.
 * See radio_get_region_bands().
 */
enum RadioRegion {
    /** No region set: only the driver's own limits apply */
    RADIO_REGION_UNSET = 0,
    RADIO_REGION_EU_433,
    RADIO_REGION_EU_868,
    RADIO_REGION_US_915,
    RADIO_REGION_AU_915,
    RADIO_REGION_NZ_865,
    RADIO_REGION_IN_865,
    RADIO_REGION_RU_868,
    RADIO_REGION_CN_470,
    RADIO_REGION_JP_920,
    RADIO_REGION_COUNT,
};

struct RadioRegionBand {
    /** Lowest allowed frequency in Hz */
    int32_t frequency_min;
    /** Highest allowed frequency in Hz */
    int32_t frequency_max;
    /** Maximum TX power in dBm */
    int8_t power_max;
};

/** Largest RX payload any supported modem can hand back in one packet. */
#define RADIO_RX_MAX_PACKET_LENGTH 255

struct RadioApi {
    /**
     * Get the radio state of the device.
     * @param[in] device the radio device
     * @param[out] state the radio state
     * @return ERROR_NONE on success
     */
    error_t (*get_radio_state)(struct Device* device, enum RadioState* state);

    /**
     * Turn the radio on or off. Requires a modulation to be set before enabling.
     * Turning on is asynchronous: observe the radio state to know when it's up.
     * @param[in] device the radio device
     * @param[in] enabled true to turn the radio on
     * @return ERROR_NONE on success
     * @retval ERROR_INVALID_STATE when enabling without a modulation set
     */
    error_t (*set_enabled)(struct Device* device, bool enabled);

    /**
     * Set the modulation scheme. Only allowed while the radio is off.
     * @param[in] device the radio device
     * @param[in] modulation the modulation scheme
     * @return ERROR_NONE on success
     * @retval ERROR_INVALID_STATE when the radio is on or turning on
     * @retval ERROR_NOT_SUPPORTED when the device supports neither TX nor RX for this modulation
     */
    error_t (*set_modulation)(struct Device* device, enum RadioModulation modulation);

    /**
     * Get the current modulation scheme.
     * @param[in] device the radio device
     * @param[out] modulation the modulation scheme
     * @return ERROR_NONE on success
     */
    error_t (*get_modulation)(struct Device* device, enum RadioModulation* modulation);

    /**
     * @param[in] device the radio device
     * @param[in] modulation the modulation scheme
     * @return true when the device can transmit with the given modulation
     */
    bool (*can_transmit)(struct Device* device, enum RadioModulation modulation);

    /**
     * @param[in] device the radio device
     * @param[in] modulation the modulation scheme
     * @return true when the device can receive with the given modulation
     */
    bool (*can_receive)(struct Device* device, enum RadioModulation modulation);

    /**
     * Set a radio parameter. See RadioParameter for units.
     * Parameters apply to the current modulation and take effect the next time the radio turns on.
     * @param[in] device the radio device
     * @param[in] parameter the parameter to set
     * @param[in] value the value to set
     * @return ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED when the parameter doesn't apply to the device or modulation
     * @retval ERROR_OUT_OF_RANGE when the value is invalid for the parameter
     */
    error_t (*set_parameter)(struct Device* device, enum RadioParameter parameter, int32_t value);

    /**
     * Get a radio parameter. See RadioParameter for units.
     * @param[in] device the radio device
     * @param[in] parameter the parameter to get
     * @param[out] value the current value
     * @return ERROR_NONE on success
     * @retval ERROR_NOT_SUPPORTED when the parameter doesn't apply to the device or modulation
     */
    error_t (*get_parameter)(struct Device* device, enum RadioParameter parameter, int32_t* value);

    /**
     * Queue a packet for transmission. The data is copied.
     * Progress is reported through the TX callbacks, starting with QUEUED.
     * @param[in] device the radio device
     * @param[in] data the packet payload
     * @param[in] length the payload length in bytes
     * @param[out] id the id assigned to this transmission (optional, can be NULL)
     * @return ERROR_NONE on success
     */
    error_t (*transmit)(struct Device* device, const uint8_t* data, size_t length, RadioTxId* id);

    /**
     * Optional: get the instantaneous RSSI, which is the noise floor when nothing is being received.
     * Only available while the radio is on.
     * @param[in] device the radio device
     * @param[out] rssi the RSSI in dBm
     * @return ERROR_NONE on success
     * @retval ERROR_INVALID_STATE when the radio is not on
     */
    error_t (*get_rssi)(struct Device* device, float* rssi);

    /**
     * Optional: check whether a packet is being received right now (preamble or sync word detected).
     * Only available while the radio is on.
     * @param[in] device the radio device
     * @param[out] receiving true when a packet is being received
     * @return ERROR_NONE on success
     * @retval ERROR_INVALID_STATE when the radio is not on
     */
    error_t (*is_receiving)(struct Device* device, bool* receiving);
};

// region State events

struct RadioStateEvent {
    /** Stamped by radio_state_event_emit(); any value passed in by the caller is ignored. */
    uint64_t timestamp;
    enum RadioState state;
};

/**
 * Number of events that can be queued per subscription before radio_state_event_emit() starts
 * returning ERROR_RESOURCE (dropping the newest event, preserving FIFO order of what's
 * already queued).
 */
#define RADIO_STATE_EVENT_QUEUE_CAPACITY 4

/**
 * Caller-owned subscription node, registered with radio_state_event_subscribe() and drained with
 * radio_state_event_poll(). Events queue by value (FIFO).
 * @warning Fields other than `bit` are for internal use only; do not read or write them directly.
 */
struct RadioStateEventSubscription {
    /** Set by radio_state_event_subscribe(). Read-only for the caller: OR it into a
     * task_event_group_wait() mask (alongside other subscriptions sharing the same
     * `event_group`) to block on this subscription and other event sources with one call. */
    uint32_t bit;

    struct {
        /** The radio device this subscription receives events for; set by radio_state_event_subscribe(). */
        struct Device* device;

        /** Caller-owned, borrowed; set by radio_state_event_subscribe(). */
        struct TaskEventGroup* event_group;

        struct RadioStateEvent queue[RADIO_STATE_EVENT_QUEUE_CAPACITY];
        uint8_t head;
        uint8_t count;

        struct RadioStateEventSubscription* next;
    } internal;
};

/**
 * Register a subscription for radio state changes emitted by @a device.
 * @warning Does not work in ISR context.
 * @param[in,out] sub subscription to register; owns the storage, must stay alive (and
 * stationary) until unsubscribed
 * @param[in] event_group caller-owned group to wait on; must outlive @a sub (i.e. be
 * destructed only after radio_state_event_unsubscribe()). To block for an event, call
 * task_event_group_wait()/task_event_group_wait_any() on this group (OR sub->bit into the mask,
 * or use _wait_any() to include every subscription sharing it), then drain with
 * radio_state_event_poll().
 * @param[in] device the radio device this subscription receives events for
 * @retval ERROR_NONE on success
 * @retval ERROR_RESOURCE @a event_group has no free bits left to claim; @a sub was not registered
 * @retval ERROR_INVALID_STATE @a sub is already registered
 */
error_t radio_state_event_subscribe(struct RadioStateEventSubscription* sub, struct TaskEventGroup* event_group, struct Device* device);

/**
 * Remove a previously registered subscription.
 * @warning Does not work in ISR context.
 * @return ERROR_NONE on success, ERROR_NOT_FOUND if no matching subscription exists
 */
error_t radio_state_event_unsubscribe(struct RadioStateEventSubscription* sub);

/**
 * Non-blocking: pop the next event for @a sub if one is already queued.
 * @warning Never blocks. To wait for an event, block in task_event_group_wait()/
 * task_event_group_wait_any() on @a sub's event group first (see radio_state_event_subscribe()),
 * then drain with this in a loop.
 * @retval ERROR_NONE @a out_event was filled
 * @retval ERROR_TIMEOUT nothing queued right now
 */
error_t radio_state_event_poll(struct RadioStateEventSubscription* sub, struct RadioStateEvent* out_event);

/**
 * Emit a radio state event to every subscriber of @a device. Called by RadioApi driver
 * implementations; not intended for consumers of the API.
 * @param[in] device the radio device the event originates from
 * @param[in] state the new radio state
 * @return ERROR_NONE on success, ERROR_NOT_FOUND if @a device has no subscribers
 */
error_t radio_state_event_emit(struct Device* device, enum RadioState state);

// endregion

// region RX events

struct RadioRxEvent {
    /** Stamped by radio_rx_event_emit(); any value passed in by the caller is ignored. */
    uint64_t timestamp;
    uint8_t data[RADIO_RX_MAX_PACKET_LENGTH];
    size_t length;
    /** Received signal strength in dBm */
    float rssi;
    /** Signal-to-noise ratio in dB, or NAN when the modem doesn't report it */
    float snr;
};

/**
 * Number of events that can be queued per subscription before radio_rx_event_emit() starts
 * returning ERROR_RESOURCE (dropping the newest event, preserving FIFO order of what's
 * already queued).
 */
#define RADIO_RX_EVENT_QUEUE_CAPACITY 8

/**
 * Caller-owned subscription node, registered with radio_rx_event_subscribe() and drained with
 * radio_rx_event_poll(). Events queue by value (FIFO): a subscription that falls behind keeps
 * every queued packet; once the queue is full, newly received packets are dropped instead.

 * @warning Fields other than `bit` are for internal use only; do not read or write them directly.
 */
struct RadioRxEventSubscription {
    /** Set by radio_rx_event_subscribe(). Read-only for the caller: OR it into a
     * task_event_group_wait() mask (alongside other subscriptions sharing the same
     * `event_group`) to block on this subscription and other event sources with one call. */
    uint32_t bit;

    struct {
        /** The radio device this subscription receives events for; set by radio_rx_event_subscribe(). */
        struct Device* device;

        /** Caller-owned, borrowed; set by radio_rx_event_subscribe(). */
        struct TaskEventGroup* event_group;

        struct RadioRxEvent queue[RADIO_RX_EVENT_QUEUE_CAPACITY];
        uint8_t head;
        uint8_t count;

        struct RadioRxEventSubscription* next;
    } internal;
};

/**
 * Register a subscription for received packets emitted by @a device.
 * @warning Does not work in ISR context.
 * @param[in,out] sub subscription to register; owns the storage, must stay alive (and
 * stationary) until unsubscribed
 * @param[in] event_group caller-owned group to wait on; must outlive @a sub (i.e. be
 * destructed only after radio_rx_event_unsubscribe()). To block for an event, call
 * task_event_group_wait()/task_event_group_wait_any() on this group (OR sub->bit into the mask,
 * or use _wait_any() to include every subscription sharing it), then drain with
 * radio_rx_event_poll().
 * @param[in] device the radio device this subscription receives events for
 * @retval ERROR_NONE on success
 * @retval ERROR_RESOURCE @a event_group has no free bits left to claim; @a sub was not registered
 * @retval ERROR_INVALID_STATE @a sub is already registered
 */
error_t radio_rx_event_subscribe(struct RadioRxEventSubscription* sub, struct TaskEventGroup* event_group, struct Device* device);

/**
 * Remove a previously registered subscription.
 * @warning Does not work in ISR context.
 * @return ERROR_NONE on success, ERROR_NOT_FOUND if no matching subscription exists
 */
error_t radio_rx_event_unsubscribe(struct RadioRxEventSubscription* sub);

/**
 * Non-blocking: pop the next event for @a sub if one is already queued.
 * @warning Never blocks. To wait for an event, block in task_event_group_wait()/
 * task_event_group_wait_any() on @a sub's event group first (see radio_rx_event_subscribe()),
 * then drain with this in a loop.
 * @retval ERROR_NONE @a out_event was filled
 * @retval ERROR_TIMEOUT nothing queued right now
 */
error_t radio_rx_event_poll(struct RadioRxEventSubscription* sub, struct RadioRxEvent* out_event);

/**
 * Emit a received-packet event to every subscriber of @a device. Called by RadioApi driver
 * implementations; not intended for consumers of the API.
 * @param[in] device the radio device the event originates from
 * @param[in] data the packet payload; copied, truncated to RADIO_RX_MAX_PACKET_LENGTH
 * @param[in] length the payload length in bytes
 * @param[in] rssi received signal strength in dBm
 * @param[in] snr signal-to-noise ratio in dB, or NAN when the modem doesn't report it
 * @return ERROR_NONE on success, ERROR_NOT_FOUND if @a device has no subscribers
 */
error_t radio_rx_event_emit(struct Device* device, const uint8_t* data, size_t length, float rssi, float snr);

// endregion

// region TX events

struct RadioTxEvent {
    /** Stamped by radio_tx_event_emit(); any value passed in by the caller is ignored. */
    uint64_t timestamp;
    RadioTxId id;
    enum RadioTransmissionState state;
};

/**
 * Number of events that can be queued per subscription before radio_tx_event_emit() starts
 * returning ERROR_RESOURCE (dropping the newest event, preserving FIFO order of what's
 * already queued).
 */
#define RADIO_TX_EVENT_QUEUE_CAPACITY 4

/**
 * Caller-owned subscription node, registered with radio_tx_event_subscribe() and drained with
 * radio_tx_event_poll(). Events queue by value (FIFO).
 * @warning Fields other than `bit` are for internal use only; do not read or write them directly.
 */
struct RadioTxEventSubscription {
    /** Set by radio_tx_event_subscribe(). Read-only for the caller: OR it into a
     * task_event_group_wait() mask (alongside other subscriptions sharing the same
     * `event_group`) to block on this subscription and other event sources with one call. */
    uint32_t bit;

    struct {
        /** The radio device this subscription receives events for; set by radio_tx_event_subscribe(). */
        struct Device* device;

        /** Caller-owned, borrowed; set by radio_tx_event_subscribe(). */
        struct TaskEventGroup* event_group;

        struct RadioTxEvent queue[RADIO_TX_EVENT_QUEUE_CAPACITY];
        uint8_t head;
        uint8_t count;

        struct RadioTxEventSubscription* next;
    } internal;
};

/**
 * Register a subscription for transmission progress emitted by @a device.
 * @warning Does not work in ISR context.
 * @param[in,out] sub subscription to register; owns the storage, must stay alive (and
 * stationary) until unsubscribed
 * @param[in] event_group caller-owned group to wait on; must outlive @a sub (i.e. be
 * destructed only after radio_tx_event_unsubscribe()). To block for an event, call
 * task_event_group_wait()/task_event_group_wait_any() on this group (OR sub->bit into the mask,
 * or use _wait_any() to include every subscription sharing it), then drain with
 * radio_tx_event_poll().
 * @param[in] device the radio device this subscription receives events for
 * @retval ERROR_NONE on success
 * @retval ERROR_RESOURCE @a event_group has no free bits left to claim; @a sub was not registered
 * @retval ERROR_INVALID_STATE @a sub is already registered
 */
error_t radio_tx_event_subscribe(struct RadioTxEventSubscription* sub, struct TaskEventGroup* event_group, struct Device* device);

/**
 * Remove a previously registered subscription.
 * @warning Does not work in ISR context.
 * @return ERROR_NONE on success, ERROR_NOT_FOUND if no matching subscription exists
 */
error_t radio_tx_event_unsubscribe(struct RadioTxEventSubscription* sub);

/**
 * Non-blocking: pop the next event for @a sub if one is already queued.
 * @warning Never blocks. To wait for an event, block in task_event_group_wait()/
 * task_event_group_wait_any() on @a sub's event group first (see radio_tx_event_subscribe()),
 * then drain with this in a loop.
 * @retval ERROR_NONE @a out_event was filled
 * @retval ERROR_TIMEOUT nothing queued right now
 */
error_t radio_tx_event_poll(struct RadioTxEventSubscription* sub, struct RadioTxEvent* out_event);

/**
 * Emit a transmission-progress event to every subscriber of @a device. Called by RadioApi driver
 * implementations; not intended for consumers of the API.
 * @param[in] device the radio device the event originates from
 * @param[in] id the transmission this event reports progress for
 * @param[in] state the transmission's new state
 * @return ERROR_NONE on success, ERROR_NOT_FOUND if @a device has no subscribers
 */
error_t radio_tx_event_emit(struct Device* device, RadioTxId id, enum RadioTransmissionState state);

// endregion

extern const struct DeviceType RADIO_TYPE;

error_t radio_get_state(struct Device* device, enum RadioState* state);
error_t radio_set_enabled(struct Device* device, bool enabled);
error_t radio_set_modulation(struct Device* device, enum RadioModulation modulation);
error_t radio_get_modulation(struct Device* device, enum RadioModulation* modulation);
bool radio_can_transmit(struct Device* device, enum RadioModulation modulation);
bool radio_can_receive(struct Device* device, enum RadioModulation modulation);
error_t radio_set_parameter(struct Device* device, enum RadioParameter parameter, int32_t value);
error_t radio_get_parameter(struct Device* device, enum RadioParameter parameter, int32_t* value);
error_t radio_transmit(struct Device* device, const uint8_t* data, size_t length, RadioTxId* id);

/** @retval ERROR_NOT_SUPPORTED when the driver doesn't implement it */
error_t radio_get_rssi(struct Device* device, float* rssi);

/** @retval ERROR_NOT_SUPPORTED when the driver doesn't implement it */
error_t radio_is_receiving(struct Device* device, bool* receiving);

// region Ownership

/** Maximum owner name length, excluding the terminator */
#define RADIO_OWNER_NAME_MAX_LENGTH 31

/**
 * Claim a radio device for exclusive use. The claim is advisory: consumers that want to configure or
 * transmit should acquire first and back off when the radio is owned by someone else. RX and other
 * event subscriptions are not affected.
 * @param[in] device the radio device
 * @param[in] owner a name that identifies the owner, truncated to RADIO_OWNER_NAME_MAX_LENGTH
 * @retval ERROR_NONE on success, or when @a owner already holds the device
 * @retval ERROR_RESOURCE_BUSY when another owner holds the device
 */
error_t radio_acquire(struct Device* device, const char* owner);

/**
 * Release a claim made with radio_acquire().
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_STATE when the device is not owned
 */
error_t radio_release(struct Device* device);

/**
 * Get the name of the current owner.
 * @param[in] device the radio device
 * @param[out] owner buffer for the owner name
 * @param[in] owner_size size of @a owner
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND when the device is not owned
 */
error_t radio_get_owner(struct Device* device, char* owner, size_t owner_size);

// endregion

// region Regulatory region

/**
 * Set the regulatory region of a radio device. While a region other than RADIO_REGION_UNSET is set:
 * radio_set_parameter() rejects a frequency outside the region's bands and a power above the region's
 * highest limit, and radio_set_enabled() rejects turning on when the configured frequency, bandwidth
 * and power don't fit a single band. Both return ERROR_OUT_OF_RANGE.
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT for an unknown region
 */
error_t radio_set_region(struct Device* device, enum RadioRegion region);

/** @return the region of the device, RADIO_REGION_UNSET when none was set */
enum RadioRegion radio_get_region(struct Device* device);

/**
 * Get the bands of a region.
 * @param[in] region the region
 * @param[out] bands the region's bands
 * @return the number of bands, 0 for RADIO_REGION_UNSET or an unknown region
 */
size_t radio_get_region_bands(enum RadioRegion region, const struct RadioRegionBand** bands);

/** @return a short name for the region, e.g. "EU_868" */
const char* radio_region_to_string(enum RadioRegion region);

/**
 * @param[in] name a short region name, e.g. "EU_868"
 * @param[out] region the region with that name
 * @retval ERROR_NOT_FOUND when no region has that name
 */
error_t radio_region_from_string(const char* name, enum RadioRegion* region);

// endregion

#ifdef __cplusplus
}
#endif
