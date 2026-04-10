#pragma once

#include <array>
#include <cstdint>

namespace tt::app::btmanage {

typedef void (*OnBtToggled)(bool enable);
typedef void (*OnScanToggled)(bool enable);
typedef void (*OnConnectPeer)(const std::array<uint8_t, 6>& addr, int profileId);
typedef void (*OnDisconnectPeer)(const std::array<uint8_t, 6>& addr, int profileId);
typedef void (*OnPairPeer)(const std::array<uint8_t, 6>& addr);
typedef void (*OnForgetPeer)(const std::array<uint8_t, 6>& addr);

struct Bindings {
    OnBtToggled onBtToggled;
    OnScanToggled onScanToggled;
    OnConnectPeer onConnectPeer;
    OnDisconnectPeer onDisconnectPeer;
    OnPairPeer onPairPeer;
    OnForgetPeer onForgetPeer;
};

} // namespace tt::app::btmanage
