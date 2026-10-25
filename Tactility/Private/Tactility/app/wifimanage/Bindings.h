#pragma once

#include <string>

namespace tt::app::wifimanage {

typedef void (*OnWifiToggled)(bool enable);
typedef void (*OnConnectSsid)(const std::string& ssid);
typedef void (*OnDisconnect)();
typedef void (*OnShowApSettings)(const std::string& ssid);
typedef void (*OnConnectToHidden)();
/** @return false when no scan is running or starting */
typedef bool (*OnRefresh)();

struct Bindings{
    OnWifiToggled onWifiToggled;
    OnConnectSsid onConnectSsid;
    OnDisconnect onDisconnect;
    OnShowApSettings onShowApSettings;
    OnConnectToHidden onConnectToHidden;
    OnRefresh onRefresh;
};

} // namespace
