#include <Tactility/app/btmanage/BtManagePrivate.h>

namespace tt::app::btmanage {

void State::setScanning(bool isScanning) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    scanning = isScanning;
}

bool State::isScanning() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return scanning;
}

void State::setRadioState(BtRadioState s) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    radioState = s;
}

BtRadioState State::getRadioState() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return radioState;
}

void State::updateScanResults() {
    // Fetch outside the lock to avoid holding it during a service call.
    std::vector<BluetoothPeer> results;
    bluetooth_service_for_each_scan_result(&results, [](const BluetoothPeer* peer, void* context) {
        static_cast<std::vector<BluetoothPeer>*>(context)->push_back(*peer);
        return true;
    });
    auto lock = mutex.asScopedLock();
    lock.lock();
    scanResults = std::move(results);
}

void State::updatePairedPeers() {
    std::vector<BluetoothPeer> peers;
    bluetooth_service_for_each_paired_peer(&peers, [](const BluetoothPeer* peer, void* context) {
        static_cast<std::vector<BluetoothPeer>*>(context)->push_back(*peer);
        return true;
    });
    auto lock = mutex.asScopedLock();
    lock.lock();
    pairedPeers = std::move(peers);
}

} // namespace tt::app::btmanage
