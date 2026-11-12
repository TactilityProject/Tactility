#pragma once

#include <Tactility/RecursiveMutex.h>

#include <bluetooth/bluetooth_service.h>

#include <vector>

namespace tt::app::btmanage {

class State final {

    mutable RecursiveMutex mutex;
    bool scanning = false;
    BtRadioState radioState = BT_RADIO_STATE_OFF;
    std::vector<BluetoothPeer> scanResults;
    std::vector<BluetoothPeer> pairedPeers;

public:
    State() = default;

    void setScanning(bool isScanning);
    bool isScanning() const;

    void setRadioState(BtRadioState state);
    BtRadioState getRadioState() const;

    void updateScanResults();
    void updatePairedPeers();

    std::vector<BluetoothPeer> getScanResults() const {
        auto lock = mutex.asScopedLock();
        lock.lock();
        return scanResults;
    }

    std::vector<BluetoothPeer> getPairedPeers() const {
        auto lock = mutex.asScopedLock();
        lock.lock();
        return pairedPeers;
    }
};

} // namespace tt::app::btmanage
