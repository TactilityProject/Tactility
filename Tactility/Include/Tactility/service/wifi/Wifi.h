#pragma once

#include <Tactility/PubSub.h>

#include <memory>
#include <string>
#include <vector>

#include "WifiApSettings.h"

#ifdef ESP_PLATFORM
#include <esp_wifi.h>
#else
#include <cstdint>
// From esp_wifi_types.h in ESP-IDF 5.2
typedef enum {
    WIFI_AUTH_OPEN = 0,         /**< authenticate mode : open */
    WIFI_AUTH_WEP,              /**< authenticate mode : WEP */
    WIFI_AUTH_WPA_PSK,          /**< authenticate mode : WPA_PSK */
    WIFI_AUTH_WPA2_PSK,         /**< authenticate mode : WPA2_PSK */
    WIFI_AUTH_WPA_WPA2_PSK,     /**< authenticate mode : WPA_WPA2_PSK */
    WIFI_AUTH_ENTERPRISE,       /**< authenticate mode : WiFi EAP security */
    WIFI_AUTH_WPA2_ENTERPRISE = WIFI_AUTH_ENTERPRISE,  /**< authenticate mode : WiFi EAP security */
    WIFI_AUTH_WPA3_PSK,         /**< authenticate mode : WPA3_PSK */
    WIFI_AUTH_WPA2_WPA3_PSK,    /**< authenticate mode : WPA2_WPA3_PSK */
    WIFI_AUTH_WAPI_PSK,         /**< authenticate mode : WAPI_PSK */
    WIFI_AUTH_OWE,              /**< authenticate mode : OWE */
    WIFI_AUTH_WPA3_ENT_192,     /**< authenticate mode : WPA3_ENT_SUITE_B_192_BIT */
    WIFI_AUTH_WPA3_EXT_PSK,     /**< authenticate mode : WPA3_PSK_EXT_KEY */
    WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE, /**< authenticate mode: WPA3_PSK + WPA3_PSK_EXT_KEY */
    WIFI_AUTH_MAX
} wifi_auth_mode_t;
#endif

namespace tt::service::wifi {

enum class WifiEvent {
    /** Radio was turned on */
    RadioStateOn,
    /** Radio is turning on. */
    RadioStateOnPending,
    /** Radio is turned off */
    RadioStateOff,
    /** Radio is turning off */
    RadioStateOffPending,
    /** Started scanning for access points */
    ScanStarted,
    /** Finished scanning for access points */ // TODO: 1 second validity
    ScanFinished,
    Disconnected,
    ConnectionPending,
    ConnectionSuccess,
    ConnectionFailed
};

enum class RadioState {
    OnPending,
    On,
    ConnectionPending,
    ConnectionActive,
    OffPending,
    Off,
};

struct ApRecord {
    std::string ssid;
    int8_t rssi;
    int32_t channel;
    wifi_auth_mode_t auth_mode;
};

/**
 * @brief Get wifi pubsub that broadcasts Event objects
 * @return PubSub
 */
std::shared_ptr<PubSub<WifiEvent>> getPubsub();

/** @return Get the current radio state */
RadioState getRadioState();

/** For logging purposes */
const char* radioStateToString(RadioState state);

/**
 * @brief Request scanning update. Returns immediately. Results are through pubsub.
 */
void scan();

/** @return true if wifi is actively scanning */
bool isScanning();

/** @return true the ssid name or empty string */
std::string getConnectionTarget();

/** @return the access points from the last scan (if any). It only contains public APs. */
std::vector<ApRecord> getScanResults();

/**
 * @brief Overrides the default scan result size of 16.
 * @param[in] records the record limit for the scan result (84 bytes per record!)
 */
void setScanRecords(uint16_t records);

/**
 * @brief Enable/disable the radio. Ignores input if desired state matches current state.
 * @param[in] enabled
 */
void setEnabled(bool enabled);

/**
 * @return the IPv4 address or empty string
 */
std::string getIp();

/**
 * @brief Connect to a network. Disconnects any existing connection.
 * Returns immediately but runs in the background. Results are through pubsub.
 * @param[in] ap
 * @param[in] remember whether to save the ap data to the settings upon successful connection
 */
void connect(const settings::WifiApSettings& ap, bool remember);

/** @brief Disconnect from the access point. Doesn't have any effect when not connected. */
void disconnect();

/** @return true if the connection isn't unencrypted. */
bool isConnectionSecure();

/** @return the RSSI value (negative number) or return 1 when not connected. */
int getRssi();

} // namespace
