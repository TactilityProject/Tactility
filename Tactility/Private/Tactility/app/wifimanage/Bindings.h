#pragma once

#include <string>

namespace tt::app::wifimanage {

typedef void (*OnWifiToggled)(void* userData, bool enable);
typedef void (*OnConnectSsid)(void* userData, const std::string& ssid);
typedef void (*OnDisconnect)(void* userData);
typedef void (*OnShowApSettings)(const std::string& ssid);
typedef void (*OnConnectToHidden)();
/** @return false when no scan is running or starting */
typedef bool (*OnRefresh)();

struct Bindings{
    /** Passed to onWifiToggled, onConnectSsid and onDisconnect */
    void* userData;
    OnWifiToggled onWifiToggled;
    OnConnectSsid onConnectSsid;
    OnDisconnect onDisconnect;
    OnShowApSettings onShowApSettings;
    OnConnectToHidden onConnectToHidden;
    OnRefresh onRefresh;
};

} // namespace
