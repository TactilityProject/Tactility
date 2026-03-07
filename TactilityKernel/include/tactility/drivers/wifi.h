#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;

enum WifiAuthenticationType {
    WIFI_AUTHENTICATION_TYPE_OPEN = 0,
    WIFI_AUTHENTICATION_TYPE_WEP,
    WIFI_AUTHENTICATION_TYPE_WPA_PSK,
    WIFI_AUTHENTICATION_TYPE_WPA2_PSK,
    WIFI_AUTHENTICATION_TYPE_WPA_WPA2_PSK,
    WIFI_AUTHENTICATION_TYPE_WPA2_ENTERPRISE,
    WIFI_AUTHENTICATION_TYPE_WPA3_PSK,
    WIFI_AUTHENTICATION_TYPE_WPA2_WPA3_PSK,
    WIFI_AUTHENTICATION_TYPE_WAPI_PSK,
    WIFI_AUTHENTICATION_TYPE_OWE,
    WIFI_AUTHENTICATION_TYPE_WPA3_ENT_192,
    WIFI_AUTHENTICATION_TYPE_WPA3_EXT_PSK,
    WIFI_AUTHENTICATION_TYPE_WPA3_EXT_PSK_MIXED_MODE,
    WIFI_AUTHENTICATION_TYPE_MAX
};

struct WifiApRecord {
    char ssid[33]; // 32 bytes + null terminator
    int8_t rssi;
    int32_t channel;
    enum WifiAuthenticationType authentication_type;
};

enum WifiRadioState {
    WIFI_RADIO_STATE_OFF,
    WIFI_RADIO_STATE_ON_PENDING,
    WIFI_RADIO_STATE_ON,
    WIFI_RADIO_STATE_OFF_PENDING,
};

enum WifiStationState {
    WIFI_STATION_STATE_DISCONNECTED,
    WIFI_STATION_STATE_CONNECTION_PENDING,
    WIFI_STATION_STATE_CONNECTED
};

enum WifiAccessPointState {
    WIFI_ACCESS_POINT_STATE_STARTED,
    WIFI_ACCESS_POINT_STATE_STOPPED,
};

enum WifiEventType {
    /** Radio state changed */
    WIFI_EVENT_TYPE_RADIO_STATE_CHANGED,
    /** WifiStationState changed */
    WIFI_EVENT_TYPE_STATION_STATE_CHANGED,
    /** WifiAccessPointState changed */
    WIFI_EVENT_TYPE_STATION_CONNECTION_RESULT,
    /** WifiAccessPointState changed */
    WIFI_EVENT_TYPE_ACCESS_POINT_STATE_CHANGED,
    /** Started scanning for access points */
    WIFI_EVENT_TYPE_SCAN_STARTED,
    /** Finished scanning for access points */
    WIFI_EVENT_TYPE_SCAN_FINISHED,
};

enum WifiStationConnectionError {
    WIFI_STATION_CONNECTION_ERROR_NONE,
    /** Wrong password */
    WIFI_STATION_CONNECTION_ERROR_WRONG_CREDENTIALS,
    /** Failed to connect in a timely manner */
    WIFI_STATION_CONNECTION_ERROR_TIMEOUT,
    /** SSID not found */
    WIFI_STATION_CONNECTION_ERROR_TARGET_NOT_FOUND,
};

struct WifiEvent {
    enum WifiEventType type;
    union {
        enum WifiRadioState radio_state;
        enum WifiStationState station_state;
        enum WifiAccessPointState access_point_state;
        enum WifiStationConnectionError connection_error;
    };
};

typedef void (*WifiEventCallback)(struct Device* device, void* callback_context, struct WifiEvent event);

struct WifiApi {
    /**
     * Get the radio state of the device.
     * @param[in] device the wifi device
     * @param[out] state the radio state
     * @return ERROR_NONE on success
     */
    error_t (*get_radio_state)(struct Device* device, enum WifiRadioState* state);

    /**
     * Get the station state of the device.
     * @param[in] device the wifi device
     * @param[out] state the station state
     * @return ERROR_NONE on success
     */
    error_t (*get_station_state)(struct Device* device, enum WifiStationState* state);

    /**
     * Get the access point state of the device.
     * @param[in] device the wifi device
     * @param[out] state the access point state
     * @return ERROR_NONE on success
     */
    error_t (*get_access_point_state)(struct Device* device, enum WifiAccessPointState* state);

    /**
     * Check if the device is currently scanning for access points.
     * @param[in] device the wifi device
     * @return true when scanning
     */
    bool (*is_scanning)(struct Device* device);

    /**
     * Start a scan for access points.
     * @param[in] device the wifi device
     * @return ERROR_NONE on success
     */
    error_t (*scan)(struct Device* device);

    /**
     * Get the scan results of the device.
     * @param[in] device the wifi device
     * @param[out] results the buffer to store the scan results
     * @param[in, out] num_results the number of scan results: it's first used as input to determine the size of the buffer, and then as output to get the actual number of results
     * @return ERROR_NONE on success
     */
    error_t (*get_scan_results)(struct Device* device, struct WifiApRecord* results, size_t* num_results);

    /**
     * Get the IPv4 address of the device.
     * @param[in] device the device
     * @param[out] ipv4 the buffer to store the IPv4 address (must be at least 16 bytes, will be null-terminated)
     * @return ERROR_NONE on success
     */
    error_t (*station_get_ipv4_address)(struct Device* device, char* ipv4);

    /**
     * Get the SSID of the access point the device is currently connected to.
     * @param[in] device the wifi device
     * @param[out] ssid the buffer to store the SSID (must be at least 33 bytes, will be null-terminated)
     * @return ERROR_NONE on success
     */
    error_t (*station_get_target_ssid)(struct Device* device, char* ssid);

    /**
     * Connect to an access point.
     * @param[in] device the wifi device
     * @param[in] ssid the SSID of the access point
     * @param[in] password the password of the access point
     * @param[in] channel the Wi-Fi channel to connect to (0 means "any" / no preference)
     * @return ERROR_NONE on success
     */
    error_t (*station_connect)(struct Device* device, const char* ssid, const char* password, int32_t channel);

    /**
     * Disconnect from the current access point.
     * @param[in] device the wifi device
     * @return ERROR_NONE on success
     */
    error_t (*station_disconnect)(struct Device* device);

    /**
     * Get the RSSI of the current access point.
     * @param[in] device the wifi device
     * @param[out] rssi the buffer to store the RSSI
     * @return ERROR_NONE on success
     */
    error_t (*station_get_rssi)(struct Device* device, int32_t* rssi);

    /**
     * Add a WifiEvent callback.
     * @param[in] device the wifi device
     * @param[in] callback_context the context to pass to the callback
     * @param[in] callback the callback function
     * @return ERROR_NONE on success
     */
    error_t (*add_event_callback)(struct Device* device, void* callback_context, WifiEventCallback callback);

    /**
     * Remove a WifiEvent callback.
     * @param[in] device the wifi device
     * @param[in] callback the callback function
     * @return ERROR_NONE on success
     */
    error_t (*remove_event_callback)(struct Device* device, WifiEventCallback callback);
};

extern const struct DeviceType WIFI_TYPE;

#ifdef __cplusplus
}
#endif
