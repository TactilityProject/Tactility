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

void State::setRadioState(bluetooth::RadioState s) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    radioState = s;
}

bluetooth::RadioState State::getRadioState() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return radioState;
}

void State::updateScanResults() {
    // Fetch outside the lock to avoid holding it during a service call.
    auto results = bluetooth::getScanResults();
    auto lock = mutex.asScopedLock();
    lock.lock();
    scanResults = std::move(results);
}

void State::updatePairedPeers() {
    auto peers = bluetooth::getPairedPeers();
    auto lock = mutex.asScopedLock();
    lock.lock();
    pairedPeers = std::move(peers);
}

} // namespace tt::app::btmanage
