#include <Tactility/hal/usb/Usb.h>
#include <Tactility/hal/usb/UsbHost.h>
#include <Tactility/settings/UsbHostSettings.h>

#include <tactility/device.h>
#include <tactility/drivers/usb_device_controller.h>
#include <tactility/drivers/usb_host.h>
#include <tactility/log.h>
#include <tactility/check.h>
#include <tactility/Memory.hpp>

#include <atomic>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

namespace tt::hal::usbhost {

constexpr auto* TAG = "UsbHost";

// Set when enabling the host failed, which is cleared by the next successful change. Starting is not retried until a reboot,
// e.g. when the USB controller is still held by the debug connection that flashed the device.
static std::atomic<bool> startFailed { false };

// The USB host comes from the devicetree and is never destructed, so the pointer stays valid without a reference
static Device* findHost() {
    Device* host = nullptr;
    if (device_get_first_by_type(&USB_HOST_TYPE, &host) != ERROR_NONE) {
        return nullptr;
    }
    device_put(host);
    return host;
}

// The ESP32-P4 has a separate USB controller for the host and for the device mode. Other targets have one, so only one mode can use it.
static constexpr bool sharesControllerWithDeviceMode() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    return false;
#else
    return true;
#endif
}

// The device mode controller and the child devices that were running before the host was started, in start order
struct StoppedDeviceMode {
    static constexpr size_t MAX_DEVICES = 8;
    Device* devices[MAX_DEVICES] = {};
    size_t count = 0;
};

static StoppedDeviceMode& stoppedDeviceMode() {
    static StoppedDeviceMode* instance = newObject<StoppedDeviceMode>();
    check(instance != nullptr);
    return *instance;
}

static Device* findDeviceController() {
    Device* controller = nullptr;
    if (device_get_first_by_type(&USB_DEVICE_CONTROLLER_TYPE, &controller) != ERROR_NONE) {
        return nullptr;
    }
    device_put(controller);
    return controller;
}

static bool collectRunningChild(Device* child, void* context) {
    auto* stopped = static_cast<StoppedDeviceMode*>(context);
    if (device_is_ready(child) && stopped->count < StoppedDeviceMode::MAX_DEVICES) {
        stopped->devices[stopped->count++] = child;
    }
    return true;
}

// The hotplug poller restarts registered devices that are stopped, so they are unregistered from it
static void startDevices(StoppedDeviceMode& stopped) {
    for (size_t i = 0; i < stopped.count; i++) {
        if (!device_is_ready(stopped.devices[i]) && device_start(stopped.devices[i]) != ERROR_NONE) {
            LOG_E(TAG, "Failed to restart %s", stopped.devices[i]->name);
        }
        device_hotplug_register(stopped.devices[i]);
    }
}

// Stopping the controller releases the class that holds it, so the children are stopped first
static bool stopDeviceMode() {
    Device* controller = findDeviceController();
    if (controller == nullptr || !device_is_ready(controller)) {
        return true;
    }

    auto& stopped = stoppedDeviceMode();
    stopped.count = 0;
    stopped.devices[stopped.count++] = controller;
    device_for_each_child(controller, &stopped, collectRunningChild);

    for (size_t i = stopped.count; i-- > 0;) {
        device_hotplug_unregister(stopped.devices[i]);
        if (device_stop(stopped.devices[i]) != ERROR_NONE) {
            LOG_E(TAG, "Failed to stop %s", stopped.devices[i]->name);
            // Whatever was stopped runs again, in start order
            StoppedDeviceMode restore;
            for (size_t j = 0; j < stopped.count; j++) {
                restore.devices[restore.count++] = stopped.devices[j];
            }
            startDevices(restore);
            stopped.count = 0;
            return false;
        }
    }
    return true;
}

static void restoreDeviceMode() {
    auto& stopped = stoppedDeviceMode();
    startDevices(stopped);
    stopped.count = 0;
}

// The hotplug poller restarts registered devices that are stopped, so a disabled host is unregistered from it
static bool apply(Device* host, bool enabled) {
    if (enabled) {
        if (device_is_ready(host)) {
            device_hotplug_register(host);
            startFailed = false;
            return true;
        }
        if (sharesControllerWithDeviceMode() && !stopDeviceMode()) {
            startFailed = true;
            return false;
        }
        if (device_start(host) != ERROR_NONE) {
            LOG_E(TAG, "Failed to start USB host");
            restoreDeviceMode();
            startFailed = true;
            return false;
        }
        device_hotplug_register(host);
        startFailed = false;
        return true;
    }

    device_hotplug_unregister(host);
    if (device_is_ready(host) && device_stop(host) != ERROR_NONE) {
        LOG_E(TAG, "Failed to stop USB host");
        device_hotplug_register(host);
        return false;
    }
    restoreDeviceMode();
    startFailed = false;
    return true;
}

void systemStart() {
    // The USB boot mode starts mass storage on the USB controller, which the host would take
    if (usb::isUsbBootMode()) {
        return;
    }

    Device* host = findHost();
    settings::usbhost::UsbHostSettings saved {};
    // Without saved settings, the devicetree status decides
    if (host != nullptr && settings::usbhost::load(saved)) {
        apply(host, saved.enabled);
    }
}

bool isAvailable() {
    return findHost() != nullptr;
}

bool hasStartFailed() {
    return startFailed;
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
