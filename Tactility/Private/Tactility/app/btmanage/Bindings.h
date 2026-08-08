#pragma once

#include <array>
#include <cstdint>

namespace tt::app::btmanage {

// `context` is this app instance's Context* (see BtManagePrivate.h) - the new app-module has no
// global "current app" accessor, so callbacks need it threaded through explicitly.
typedef void (*OnBtToggled)(void* context, bool enable);
typedef void (*OnScanToggled)(void* context, bool enable);
typedef void (*OnConnectPeer)(const std::array<uint8_t, 6>& addr, int profileId);
typedef void (*OnDisconnectPeer)(const std::array<uint8_t, 6>& addr, int profileId);
typedef void (*OnPairPeer)(void* context, const std::array<uint8_t, 6>& addr);
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
