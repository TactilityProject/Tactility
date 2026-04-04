#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

typedef void (*BtEventCallback)(struct Device* device, void* context, struct BtEvent event);

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
     * Connect to a peer using the specified profile.
     * @param[in] device the bluetooth device
     * @param[in] addr the peer address
     * @param[in] profile the profile to connect with
     * @return ERROR_NONE on success
     */
    error_t (*connect)(struct Device* device, const BtAddr addr, enum BtProfileId profile);

    /**
     * Disconnect a peer from the specified profile.
     * @param[in] device the bluetooth device
     * @param[in] addr the peer address
     * @param[in] profile the profile to disconnect from
     * @return ERROR_NONE on success
     */
    error_t (*disconnect)(struct Device* device, const BtAddr addr, enum BtProfileId profile);

    /**
     * Add an event callback.
     * @param[in] device the bluetooth device
     * @param[in] context context pointer passed to the callback
     * @param[in] callback the callback function
     * @return ERROR_NONE on success
     */
    error_t (*add_event_callback)(struct Device* device, void* context, BtEventCallback callback);

    /**
     * Remove a previously added event callback.
     * @param[in] device the bluetooth device
     * @param[in] callback the callback to remove
     * @return ERROR_NONE on success
     */
    error_t (*remove_event_callback)(struct Device* device, BtEventCallback callback);

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
     * Notify the driver that a HID host connection is in progress or complete.
     * Called by the Tactility HID host module to prevent name resolution from
     * initiating a simultaneous central connection (BLE_HS_EALREADY).
     * @param[in] device the bluetooth device
     * @param[in] active true when HID host is connecting/connected, false when idle
     */
    void (*set_hid_host_active)(struct Device* device, bool active);

    /**
     * Fire an event through all registered event callbacks.
     * Used by the Tactility HID host module to inject profile-state events that
     * originate outside the platform driver (e.g. HID host connect/disconnect).
     */
    void (*fire_event)(struct Device* device, struct BtEvent event);
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
error_t bluetooth_scan_start(struct Device* device);
error_t bluetooth_scan_stop(struct Device* device);
bool    bluetooth_is_scanning(struct Device* device);
error_t bluetooth_pair(struct Device* device, const BtAddr addr);
error_t bluetooth_unpair(struct Device* device, const BtAddr addr);
error_t bluetooth_connect(struct Device* device, const BtAddr addr, enum BtProfileId profile);
error_t bluetooth_disconnect(struct Device* device, const BtAddr addr, enum BtProfileId profile);
error_t bluetooth_add_event_callback(struct Device* device, void* context, BtEventCallback callback);
error_t bluetooth_remove_event_callback(struct Device* device, BtEventCallback callback);
error_t bluetooth_set_device_name(struct Device* device, const char* name);
error_t bluetooth_get_device_name(struct Device* device, char* buf, size_t buf_len);
void    bluetooth_set_hid_host_active(struct Device* device, bool active);
void    bluetooth_fire_event(struct Device* device, struct BtEvent event);

#ifdef __cplusplus
}
#endif
