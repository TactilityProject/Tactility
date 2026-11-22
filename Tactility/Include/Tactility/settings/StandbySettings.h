#pragma once

#include <map>
#include <string>

namespace tt::settings::standby {

/** What an input device is, which decides whether it wakes the device by default */
enum class WakeKind {
    Keyboard,
    Touch,
    Encoder,
    Button
};

struct StandbySettings {
    /** The display stays on with a dimmed backlight, showing the standby screen */
    bool keepDisplay;
    bool keepWifi;
    bool keepBluetooth;
    bool keepMesh;
    /** Wake source choices by device name. Devices that are missing use the default of their kind. */
    ::std::map<::std::string, bool> wake;
};

/** Keyboards, touch and encoders wake the device, buttons don't */
bool isWakeDefault(WakeKind kind);

bool isWakeEnabled(const StandbySettings& settings, const ::std::string& deviceName, WakeKind kind);

bool load(StandbySettings& settings);

StandbySettings loadOrGetDefault();

StandbySettings getDefault();

bool save(const StandbySettings& settings);

} // namespace tt::settings::standby
