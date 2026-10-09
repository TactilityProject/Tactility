#pragma once

#include <Tactility/RecursiveMutex.h>

#include <tactility/drivers/wifi.h>

#include <string>
#include <vector>

namespace tt::app::wifimanage {

/**
 * View's state
 */
class State final {

    RecursiveMutex mutex;
    bool scanning = false;
    bool scannedAfterRadioOn = false;
    bool listRefreshRequested = true;
    WifiRadioState radioState = WIFI_RADIO_STATE_OFF;
    WifiStationState stationState = WIFI_STATION_STATE_DISCONNECTED;
    std::string connectionTarget;
    std::vector<WifiApRecord> apRecords;
    std::string connectSsid;

public:
    State() = default;

    void setScanning(bool isScanning);
    bool isScanning() const;

    bool hasScannedAfterRadioOn() const { return scannedAfterRadioOn; }

    /** The next finished scan updates the AP records and rebuilds the network list */
    void requestListRefresh();
    /** @return true if a list refresh was requested, clearing the request */
    bool takeListRefreshRequest();

    void setRadioState(WifiRadioState state);
    WifiRadioState getRadioState() const;

    void setStationState(WifiStationState state);
    WifiStationState getStationState() const;

    /** @param[in] ssid the SSID the station is connected or connecting to, or an empty string */
    void setConnectionTarget(const std::string& ssid);
    std::string getConnectionTarget() const;

    void updateApRecords(Device* device);

    template <std::invocable<const std::vector<WifiApRecord>&> Func>
    void withApRecords(Func&& onApRecords) const {
        mutex.withLock([&] {
            std::invoke(std::forward<Func>(onApRecords), apRecords);
        });
    }

    std::vector<WifiApRecord> getApRecords() const {
        auto lock = mutex.asScopedLock();
        lock.lock();
        return apRecords;
    }

    void setConnectSsid(const std::string& ssid);
    std::string getConnectSsid() const;
};

} // namespace
