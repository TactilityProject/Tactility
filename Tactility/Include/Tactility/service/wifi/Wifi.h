#pragma once

#include <Tactility/PubSub.h>

#include <memory>
#include <string>
#include <vector>

#include <tactility/drivers/wifi.h>

#include "WifiApSettings.h"

namespace tt::service::wifi {

/** The kernel wifi driver's event type: a WifiEventType tag plus a union
 * payload (radio_state/station_state/access_point_state/connection_error
 * depending on the tag). See tactility/drivers/wifi.h. */
using WifiEvent = ::WifiEvent;

enum class RadioState {
    OnPending,
    On,
    ConnectionPending,
    ConnectionActive,
    OffPending,
    Off,
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
std::vector<WifiApRecord> getScanResults();

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
