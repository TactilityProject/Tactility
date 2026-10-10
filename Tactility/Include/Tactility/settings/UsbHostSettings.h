#pragma once

namespace tt::settings::usbhost {

struct UsbHostSettings {
    bool enabled;
};

bool load(UsbHostSettings& settings);

UsbHostSettings loadOrGetDefault();

UsbHostSettings getDefault();

bool save(const UsbHostSettings& settings);

} // namespace tt::settings::usbhost
