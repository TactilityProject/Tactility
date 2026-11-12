#pragma once

#include <tactility/drivers/bluetooth.h>

namespace tt::app::btmanage {

// `context` is this app instance's Context* (see BtManagePrivate.h) - the new app-module has no
// global "current app" accessor, so callbacks need it threaded through explicitly.
typedef void (*OnBtToggled)(void* context, bool enable);
typedef void (*OnScanToggled)(void* context, bool enable);
typedef void (*OnConnectPeer)(const BtAddr addr, BtProfileId profile);
typedef void (*OnDisconnectPeer)(const BtAddr addr, BtProfileId profile);
typedef void (*OnPairPeer)(void* context, const BtAddr addr);
typedef void (*OnForgetPeer)(const BtAddr addr);

struct Bindings {
    OnBtToggled onBtToggled;
    OnScanToggled onScanToggled;
    OnConnectPeer onConnectPeer;
    OnDisconnectPeer onDisconnectPeer;
    OnPairPeer onPairPeer;
    OnForgetPeer onForgetPeer;
};

} // namespace tt::app::btmanage
