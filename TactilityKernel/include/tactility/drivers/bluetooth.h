#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;
struct DeviceType;

// ---- Device name ----

/**
 * Maximum BLE device name length in bytes, excluding the NUL terminator.
 * Must match CONFIG_BT_NIMBLE_GAP_DEVICE_NAME_MAX_LEN (set in device.py for BT devices).
 * ble_svc_gap_device_name_set() returns BLE_HS_EINVAL for names longer than this.
 */
#define BLE_DEVICE_NAME_MAX 64

// ---- Identity ----

/**
 * Maximum BLE identity length in bytes, excluding the NUL terminator.
 * The identity is never advertised: it is hashed into the radio's advertising address and
 * used to select which bond store the radio loads.
 */
#define BLE_IDENTITY_MAX 64

// ---- Address ----

#define BT_ADDR_LEN 6

typedef uint8_t BtAddr[BT_ADDR_LEN];

// ---- Radio ----

enum BtRadioState {
    BT_RADIO_STATE_OFF,
    BT_RADIO_STATE_ON_PENDING,
    BT_RADIO_STATE_ON,
    BT_RADIO_STATE_OFF_PENDING,
};

// ---- Peer record ----

#define BT_NAME_MAX 248

struct BtPeerRecord {
    BtAddr addr;
    /** BLE address type (BLE_ADDR_PUBLIC=0, BLE_ADDR_RANDOM=1, etc.) */
    uint8_t addr_type;
    char name[BT_NAME_MAX + 1];
    int8_t rssi;
    bool paired;
    bool connected;
};

// ---- Profile identifiers ----

enum BtProfileId {
    /** Connect to a BLE HID device (keyboard, mouse, gamepad) */
    BT_PROFILE_HID_HOST,
    /** Present this device as a BLE HID peripheral (keyboard, gamepad) */
    BT_PROFILE_HID_DEVICE,
    /** BLE SPP serial port (Nordic UART Service / custom GATT) */
    BT_PROFILE_SPP,
    /** BLE MIDI (GATT-based) */
    BT_PROFILE_MIDI,
};

enum BtProfileState {
    BT_PROFILE_STATE_IDLE,
    BT_PROFILE_STATE_CONNECTING,
    BT_PROFILE_STATE_CONNECTED,
    BT_PROFILE_STATE_DISCONNECTING,
};

// ---- Events ----

enum BtEventType {
    /** Radio state changed */
    BT_EVENT_RADIO_STATE_CHANGED,
    /** Started scanning for peers */
    BT_EVENT_SCAN_STARTED,
    /** Finished scanning for peers */
    BT_EVENT_SCAN_FINISHED,
    /** A new peer was found during scan */
    BT_EVENT_PEER_FOUND,
    /** Pairing requires user confirmation (passkey displayed or entry required) */
    BT_EVENT_PAIR_REQUEST,
    /** Pairing attempt completed */
    BT_EVENT_PAIR_RESULT,
    /** A peer's connection state changed */
    BT_EVENT_CONNECT_STATE_CHANGED,
    /** A profile's state changed */
    BT_EVENT_PROFILE_STATE_CHANGED,
    /** Data was received on the BLE SPP (NUS) RX characteristic */
    BT_EVENT_SPP_DATA_RECEIVED,
    /** Data was received on the BLE MIDI I/O characteristic */
    BT_EVENT_MIDI_DATA_RECEIVED,
};

enum BtPairResult {
    BT_PAIR_RESULT_SUCCESS,
    BT_PAIR_RESULT_FAILED,
    BT_PAIR_RESULT_REJECTED,
    /** Stale bond detected and removed; fresh pairing will follow */
    BT_PAIR_RESULT_BOND_LOST,
};

struct BtPairRequestData {
    BtAddr addr;
    uint32_t passkey; /**< Passkey to display (0 if not applicable) */
    bool needs_confirmation; /**< true: just confirm, false: user must enter passkey */
};

struct BtPairResultData {
    BtAddr addr;
    enum BtPairResult result;
    /** Profile active when pairing completed (BtProfileId value) */
    int profile;
};

struct BtProfileStateData {
    BtAddr addr;
    enum BtProfileId profile;
    enum BtProfileState state;
};

struct BtEvent {
    enum BtEventType type;
    union {
        enum BtRadioState radio_state;
        struct BtPeerRecord peer;
        struct BtPairRequestData pair_request;
        struct BtPairResultData pair_result;
        struct BtProfileStateData profile_state;
    };
};

/** Number of events a BtEventSubscription can hold before the driver starts dropping
 * the newest event for it (still delivered to any other matching subscription). Generous: a
 * device fires these serially, one at a time, not in genuinely concurrent bursts. */
#define BT_EVENT_QUEUE_CAPACITY 4

/**
 * Caller-owned subscription node, registered with bluetooth_event_subscribe() and polled with
 * bluetooth_event_poll(). Like wifi_event, queues events by value (FIFO) rather than coalescing
 * to the latest one.
 * @warning Fields other than `bit` are for internal use only; do not read or write them
 * directly.
 */
struct BtEventSubscription {
    /** Set by bluetooth_event_subscribe(). Read-only for the caller: OR it into a
     * task_event_group_wait() mask (alongside other subscriptions sharing the same
     * `event_group`) to block on this subscription and other event sources with one call. */
    uint32_t bit;

    struct {
        /** Caller-owned, borrowed; set by bluetooth_event_subscribe(). */
        struct TaskEventGroup* event_group;
        /** Guards `queue`/`head`/`count`/`closed` between the driver publishing events (driver thread)
         * and bluetooth_event_poll() (caller's thread) - the two live in different translation
         * units with no other shared lock. */
        struct Mutex ring_mutex;
        struct BtEvent queue[BT_EVENT_QUEUE_CAPACITY];
        uint8_t head;
        uint8_t count;
        /** Set under `ring_mutex` when the device is torn down while still subscribed (see
         * esp32_ble_stop_device()). `ring_mutex` stays constructed - only the subscription's own
         * bluetooth_event_unsubscribe() destructs it, since that can't race its own polling. */
        bool closed;
        /** True while `ring_mutex` is constructed. Read without locking `ring_mutex`: it's what
         * decides whether locking it is safe. */
        bool constructed;

        struct BtEventSubscription* next;
    } internal;
};

// ---- Top-level Bluetooth API ----

struct BluetoothApi {
    /**
     * Get the radio state.
     * @param[in] device the bluetooth device
     * @param[out] state the current radio state
     * @return ERROR_NONE on success
     */
    error_t (*get_radio_state)(struct Device* device, enum BtRadioState* state);

    /**
     * Enable or disable the Bluetooth radio.
     * @param[in] device the bluetooth device
     * @param[in] enabled true to enable, false to disable
     * @return ERROR_NONE on success
     */
    error_t (*set_radio_enabled)(struct Device* device, bool enabled);

    /**
     * Start scanning for nearby BLE devices.
     * @param[in] device the bluetooth device
     * @return ERROR_NONE on success
     */
    error_t (*scan_start)(struct Device* device);

    /**
     * Stop an active scan.
     * @param[in] device the bluetooth device
     * @return ERROR_NONE on success
     */
    error_t (*scan_stop)(struct Device* device);

    /**
     * @param[in] device the bluetooth device
     * @return true when a scan is in progress
     */
    bool (*is_scanning)(struct Device* device);

    /**
     * @param[in] device the bluetooth device
     * @return true when a peripheral profile (HID device, SPP or MIDI) is active
     */
    bool (*is_peripheral_active)(struct Device* device);

    /**
     * Initiate pairing with a peer.
     * @param[in] device the bluetooth device
     * @param[in] addr the peer address
     * @return ERROR_NONE on success
     */
    error_t (*pair)(struct Device* device, const BtAddr addr);

    /**
     * Remove a previously paired peer.
     * @param[in] device the bluetooth device
     * @param[in] addr the peer address
     * @return ERROR_NONE on success
     */
    error_t (*unpair)(struct Device* device, const BtAddr addr);

    /**
     * Get the list of currently paired peers.
     * @param[in] device the bluetooth device
     * @param[out] out the buffer to write records into (may be NULL to query count only)
     * @param[in, out] count in: capacity of out, out: actual number of paired peers
     * @return ERROR_NONE on success
     */
    error_t (*get_paired_peers)(struct Device* device, struct BtPeerRecord* out, size_t* count);

    /**
     * Connect to a peer as the central device.
     * Central role only, HID host is the only central profile today. Peripheral profiles are
     * started with their own bluetooth_<profile>_start() call instead.
     * @param[in] device the bluetooth device
     * @param[in] addr the peer address
     * @return ERROR_NONE on success
     */
    error_t (*host_connect)(struct Device* device, const BtAddr addr);

    /**
     * Disconnect the peer connected as the central device.
     * @param[in] device the bluetooth device
     * @param[in] addr the peer address
     * @return ERROR_NONE on success
     */
    error_t (*host_disconnect)(struct Device* device, const BtAddr addr);

    /**
     * Register a subscription for this device's BtEvents.
     * @warning Does not work in ISR context.
     * @param[in] device the bluetooth device
     * @param[in,out] sub subscription to register; owns the storage, must stay alive (and
     * stationary) until unsubscribed
     * @param[in] event_group caller-owned group to wait on; must outlive @a sub (i.e. be
     * destructed only after event_unsubscribe()). To block for an event, call
     * task_event_group_wait()/task_event_group_wait_any() on this group (OR sub->bit into the
     * mask, or use _wait_any() to include every subscription sharing it), then drain with
     * bluetooth_event_poll().
     * @retval ERROR_NONE on success
     * @retval ERROR_RESOURCE @a event_group has no free bits left to claim; @a sub was not registered
     * @retval ERROR_INVALID_STATE @a sub is already registered
     */
    error_t (*event_subscribe)(struct Device* device, struct BtEventSubscription* sub, struct TaskEventGroup* event_group);

    /**
     * Remove a previously registered subscription.
     * @warning Does not work in ISR context.
     * @param[in] device the bluetooth device
     * @param[in] sub subscription to remove, as passed to event_subscribe()
     * @return ERROR_NONE on success, ERROR_NOT_FOUND if no matching subscription exists
     */
    error_t (*event_unsubscribe)(struct Device* device, struct BtEventSubscription* sub);

    /**
     * Set the BLE device name used in advertising and the GAP service.
     * Can be called before or after the radio is enabled.
     * If called while advertising is active, advertising restarts with the new name.
     * @param[in] device the bluetooth device
     * @param[in] name   NUL-terminated name (max BLE_DEVICE_NAME_MAX bytes)
     * @return ERROR_NONE on success, ERROR_INVALID_ARGUMENT if name is too long or NULL
     */
    error_t (*set_device_name)(struct Device* device, const char* name);

    /**
     * Get the current BLE device name.
     * @param[in]  device  the bluetooth device
     * @param[out] buf     buffer to write the name into
     * @param[in]  buf_len size of buf (must be >= BLE_DEVICE_NAME_MAX + 1)
     * @return ERROR_NONE on success
     */
    error_t (*get_device_name)(struct Device* device, char* buf, size_t buf_len);

    /**
     * Set the stable identity this radio advertises under.
     * The identity determines the static random advertising address and which bond set is
     * loaded, so different identities appear as separate bonded devices on the peer even
     * though they share one controller. Only its hash is used, the string is never transmitted.
     *
     * Defaults to the device name (see set_device_name()). While the radio is off the change
     * applies on the next enable. While it is on the change is pending until the next
     * peripheral profile start with the radio idle, or until the radio cycles. get_identity()
     * reports the identity in use.
     *
     * @param[in] device   the bluetooth device
     * @param[in] identity NUL-terminated identity (max BLE_IDENTITY_MAX bytes), NULL or ""
     *                     reverts to the device name
     * @return ERROR_NONE on success
     * @return ERROR_INVALID_ARGUMENT if identity is too long
     */
    error_t (*set_identity)(struct Device* device, const char* identity);

    /**
     * Get the identity the radio is currently advertising under (the resolved one, i.e. the
     * device name when no explicit identity was ever set).
     * @param[in]  device  the bluetooth device
     * @param[out] buf     buffer to write the NUL-terminated identity into
     * @param[in]  buf_len size of buf (must be >= BLE_IDENTITY_MAX + 1)
     * @return ERROR_NONE on success
     */
    error_t (*get_identity)(struct Device* device, char* buf, size_t buf_len);
};

extern const struct DeviceType BLUETOOTH_TYPE;

// ---- Public C API ----
// These are the only functions external code should call.
// The BluetoothApi struct above is the internal driver interface only.

/**
 * Find the first ready Bluetooth device.
 * Use this instead of referencing BLUETOOTH_TYPE directly from external apps,
 * since data symbols may not be exported by the ELF loader.
 * @return the first ready Device of BLUETOOTH_TYPE, or NULL if none found.
 */
struct Device* bluetooth_find_first_ready_device(void);

error_t bluetooth_get_radio_state(struct Device* device, enum BtRadioState* state);
error_t bluetooth_set_radio_enabled(struct Device* device, bool enabled);
/** @return true when the radio is on or turning on */
bool    bluetooth_is_radio_on_or_pending(struct Device* device);
error_t bluetooth_scan_start(struct Device* device);
error_t bluetooth_scan_stop(struct Device* device);
bool    bluetooth_is_scanning(struct Device* device);
/** @return true when a peripheral profile (HID device, SPP or MIDI) is active */
bool    bluetooth_is_peripheral_active(struct Device* device);
error_t bluetooth_pair(struct Device* device, const BtAddr addr);
error_t bluetooth_unpair(struct Device* device, const BtAddr addr);
error_t bluetooth_get_paired_peers(struct Device* device, struct BtPeerRecord* out, size_t* count);

/** Connect to a peer as the central device (HID host). */
error_t bluetooth_host_connect(struct Device* device, const BtAddr addr);
/** Disconnect the peer connected as the central device. */
error_t bluetooth_host_disconnect(struct Device* device, const BtAddr addr);
error_t bluetooth_set_device_name(struct Device* device, const char* name);
error_t bluetooth_get_device_name(struct Device* device, char* buf, size_t buf_len);

/**
 * Set the stable identity this radio advertises under (see BluetoothApi::set_identity).
 * Defaults to the device name when never set. Applies on the next radio enable, or at the
 * next peripheral profile start while the radio is on and idle.
 * @param[in] device   the bluetooth device
 * @param[in] identity NUL-terminated identity (max BLE_IDENTITY_MAX bytes), NULL or "" reverts
 *                     to the device name
 * @return ERROR_NONE on success, ERROR_INVALID_ARGUMENT if too long
 */
error_t bluetooth_set_identity(struct Device* device, const char* identity);

/**
 * Get the identity the radio is currently advertising under.
 * @param[in]  device  the bluetooth device
 * @param[out] buf     buffer to write the NUL-terminated identity into
 * @param[in]  buf_len size of buf (must be >= BLE_IDENTITY_MAX + 1)
 * @return ERROR_NONE on success
 */
error_t bluetooth_get_identity(struct Device* device, char* buf, size_t buf_len);

/**
 * Register a subscription for @a device's BtEvents.
 * @warning Does not work in ISR context.
 * @param[in] device the bluetooth device
 * @param[in,out] sub subscription to register; owns the storage, must stay alive (and
 * stationary) until unsubscribed
 * @param[in] event_group caller-owned group to wait on; must outlive @a sub. To block for an
 * event, call task_event_group_wait()/task_event_group_wait_any() on this group (OR sub->bit
 * into the mask, or use _wait_any() to include every subscription sharing it), then drain with
 * bluetooth_event_poll().
 * @retval ERROR_NONE on success
 * @retval ERROR_RESOURCE @a event_group has no free bits left to claim; @a sub was not registered
 * @retval ERROR_INVALID_STATE @a sub is already registered
 */
error_t bluetooth_event_subscribe(struct Device* device, struct BtEventSubscription* sub, struct TaskEventGroup* event_group);

/**
 * Remove a previously registered subscription.
 * @warning Does not work in ISR context.
 * @return ERROR_NONE on success, ERROR_NOT_FOUND if no matching subscription exists
 */
error_t bluetooth_event_unsubscribe(struct Device* device, struct BtEventSubscription* sub);

/**
 * Non-blocking: pop the next event for @a sub if one is already queued.
 * @warning Never blocks. To wait for an event, block in task_event_group_wait()/
 * task_event_group_wait_any() on @a sub's event group first (see bluetooth_event_subscribe()),
 * then drain with this in a loop.
 * @retval ERROR_NONE @a out_event was filled
 * @retval ERROR_TIMEOUT nothing queued right now
 */
error_t bluetooth_event_poll(struct BtEventSubscription* sub, struct BtEvent* out_event);

#ifdef __cplusplus
}
#endif
