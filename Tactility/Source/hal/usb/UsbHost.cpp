#include <Tactility/hal/usb/UsbHost.h>
#include <Tactility/settings/UsbHostSettings.h>

#include <tactility/device.h>
#include <tactility/drivers/usb_host.h>
#include <tactility/log.h>

namespace tt::hal::usbhost {

constexpr auto* TAG = "UsbHost";

// The USB host comes from the devicetree and is never destructed, so the pointer stays valid without a reference
static Device* findHost() {
    Device* host = nullptr;
    if (device_get_first_by_type(&USB_HOST_TYPE, &host) != ERROR_NONE) {
        return nullptr;
    }
    device_put(host);
    return host;
}

// The hotplug poller restarts registered devices that are stopped, so a disabled host is unregistered from it
static bool apply(Device* host, bool enabled) {
    if (enabled) {
        if (!device_is_ready(host) && device_start(host) != ERROR_NONE) {
            LOG_E(TAG, "Failed to start USB host");
            return false;
        }
        device_hotplug_register(host);
        return true;
    }

    device_hotplug_unregister(host);
    if (device_is_ready(host) && device_stop(host) != ERROR_NONE) {
        LOG_E(TAG, "Failed to stop USB host");
        device_hotplug_register(host);
        return false;
    }
    return true;
}

void systemStart() {
    Device* host = findHost();
    if (host != nullptr) {
        apply(host, settings::usbhost::loadOrGetDefault().enabled);
    }
}

bool isAvailable() {
    return findHost() != nullptr;
}

bool isEnabled() {
    Device* host = findHost();
    return host != nullptr && device_is_ready(host);
}

bool setEnabled(bool enabled) {
    Device* host = findHost();
    if (host == nullptr) {
        return false;
    }
    if (!settings::usbhost::save({ .enabled = enabled })) {
        LOG_W(TAG, "Failed to save settings");
    }
    return apply(host, enabled);
}

} // namespace tt::hal::usbhost
