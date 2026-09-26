#include <Tactility/app/wifimanage/WifiManagePrivate.h>

namespace tt::app::wifimanage {

constexpr size_t SCAN_RECORD_LIMIT = 16;

void State::setScanning(bool isScanning) {
    mutex.lock();
    scanning = isScanning;
    scannedAfterRadioOn |= isScanning;
    mutex.unlock();
}

void State::setRadioState(WifiRadioState state) {
    mutex.lock();
    radioState = state;
    if (radioState == WIFI_RADIO_STATE_OFF) {
        scannedAfterRadioOn = false;
    }
    mutex.unlock();
}

WifiRadioState State::getRadioState() const {
    mutex.lock();
    auto result = radioState;
    mutex.unlock();
    return result;
}

void State::setStationState(WifiStationState state) {
    mutex.lock();
    stationState = state;
    mutex.unlock();
}

WifiStationState State::getStationState() const {
    mutex.lock();
    auto result = stationState;
    mutex.unlock();
    return result;
}

void State::setConnectionTarget(const std::string& ssid) {
    mutex.lock();
    connectionTarget = ssid;
    mutex.unlock();
}

std::string State::getConnectionTarget() const {
    mutex.lock();
    auto result = connectionTarget;
    mutex.unlock();
    return result;
}

bool State::isScanning() const {
    mutex.lock();
    bool result = scanning;
    mutex.unlock();
    return result;
}

void State::updateApRecords(Device* device) {
    std::vector<WifiApRecord> records(SCAN_RECORD_LIMIT);
    size_t count = records.size();
    if (device == nullptr || wifi_get_scan_results(device, records.data(), &count) != ERROR_NONE) {
        count = 0;
    }
    records.resize(count);

    mutex.lock();
    apRecords = std::move(records);
    mutex.unlock();
}

void State::setConnectSsid(const std::string& ssid) {
    mutex.lock();
    connectSsid = ssid;
    mutex.unlock();
}

std::string State::getConnectSsid() const {
    mutex.lock();
    auto result = connectSsid;
    return result;
}

} // namespace
