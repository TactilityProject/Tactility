#pragma once

#include <Tactility/bluetooth/Bluetooth.h>
#include <Tactility/RecursiveMutex.h>

namespace tt::app::btmanage {

class State final {

    mutable RecursiveMutex mutex;
    bool scanning = false;
    bluetooth::RadioState radioState = bluetooth::RadioState::Off;
    std::vector<bluetooth::PeerRecord> scanResults;
    std::vector<bluetooth::PeerRecord> pairedPeers;

public:
    State() = default;

    void setScanning(bool isScanning);
    bool isScanning() const;

    void setRadioState(bluetooth::RadioState state);
    bluetooth::RadioState getRadioState() const;

    void updateScanResults();
    void updatePairedPeers();

    std::vector<bluetooth::PeerRecord> getScanResults() const {
        auto lock = mutex.asScopedLock();
        lock.lock();
        return scanResults;
    }

    std::vector<bluetooth::PeerRecord> getPairedPeers() const {
        auto lock = mutex.asScopedLock();
        lock.lock();
        return pairedPeers;
    }
};

} // namespace tt::app::btmanage
