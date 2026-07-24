#include <Tactility/lvgl/Statusbar.h>

#include <tactility/lvgl_module.h>
#include "tactility/module.h"

#include <Tactility/Mutex.h>
#include <Tactility/Timer.h>
#include <Tactility/bluetooth/Bluetooth.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/ServicePaths.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/service/wifi/Wifi.h>
#include <tactility/gps_service.h>
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/bluetooth.h>
#include <tactility/drivers/bluetooth_midi.h>
#include <tactility/drivers/bluetooth_serial.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/drivers/usb_host_hid.h>
#include <tactility/drivers/usb_host_midi.h>
#include <tactility/drivers/usb_host_msc.h>
#include <tactility/filesystem/file_system.h>

#include <tactility/lvgl_icon_statusbar.h>

#include <cstring>

#include <tactility/log.h>

namespace tt::service::statusbar {

constexpr auto* TAG = "StatusbarService";

// GPS
extern const ServiceManifest manifest;

const char* getWifiStatusIconForRssi(int rssi) {
    if (rssi >= -60) {
        return LVGL_ICON_STATUSBAR_SIGNAL_WIFI_4_BAR;
    } else if (rssi >= -70) {
        return LVGL_ICON_STATUSBAR_NETWORK_WIFI_3_BAR;
    } else if (rssi >= -80) {
        return LVGL_ICON_STATUSBAR_NETWORK_WIFI_2_BAR;
    } else if (rssi >= -90) {
        return LVGL_ICON_STATUSBAR_NETWORK_WIFI_1_BAR;
    } else {
        return LVGL_ICON_STATUSBAR_SIGNAL_WIFI_BAD;
    }
}

static const char* getWifiStatusIcon(wifi::RadioState state) {
    int rssi;
    switch (state) {
        using enum wifi::RadioState;
        case On:
        case OnPending:
        case ConnectionPending:
            return LVGL_ICON_STATUSBAR_SIGNAL_WIFI_0_BAR;
        case OffPending:
        case Off:
            return LVGL_ICON_STATUSBAR_SIGNAL_WIFI_OFF;
        case ConnectionActive:
            rssi = wifi::getRssi();
            return getWifiStatusIconForRssi(rssi);
        default:
            check(false, "not implemented");
    }
}

static const char* getBluetoothStatusIcon(tt::bluetooth::RadioState state, bool scanning, bool connected) {
    switch (state) {
        using enum tt::bluetooth::RadioState;
        case Off:
        case OffPending:
            return nullptr; // hidden when off
        case OnPending:
        case On:
            if (connected) return LVGL_ICON_STATUSBAR_BLUETOOTH_CONNECTED;
            if (scanning)  return LVGL_ICON_STATUSBAR_BLUETOOTH_SEARCHING;
            return LVGL_ICON_STATUSBAR_BLUETOOTH;
    }
    return nullptr;
}

static const char* getSdCardStatusIcon(bool mounted) {
    if (mounted) return LVGL_ICON_STATUSBAR_SD_CARD;
    return LVGL_ICON_STATUSBAR_SD_CARD_ALERT;
}

static const char* getPowerStatusIcon() {
    // TODO: Support multiple power devices?
    Device* power = nullptr;
    device_for_each_of_type(&POWER_SUPPLY_TYPE, &power, [](Device* device, void* context) {
        if (device_is_ready(device) && power_supply_supports_property(device, POWER_SUPPLY_PROP_CAPACITY)) {
            *static_cast<Device**>(context) = device;
            return false;
        }
        return true;
    });

    if (power == nullptr) {
        return nullptr;
    }

    PowerSupplyPropertyValue charge_level;
    if (power_supply_get_property(power, POWER_SUPPLY_PROP_CAPACITY, &charge_level) != ERROR_NONE) {
        return nullptr;
    }

    int charge = charge_level.int_value;

    if (charge >= 95) {
        return LVGL_ICON_STATUSBAR_BATTERY_ANDROID_FRAME_FULL;
    } else if (charge >= 80) {
        return LVGL_ICON_STATUSBAR_BATTERY_ANDROID_FRAME_6;
    } else if (charge >= 64) {
        return LVGL_ICON_STATUSBAR_BATTERY_ANDROID_FRAME_5;
    } else if (charge >= 48) {
        return LVGL_ICON_STATUSBAR_BATTERY_ANDROID_FRAME_4;
    } else if (charge >= 32) {
        return LVGL_ICON_STATUSBAR_BATTERY_ANDROID_FRAME_3;
    } else if (charge >= 16) {
        return LVGL_ICON_STATUSBAR_BATTERY_ANDROID_FRAME_2;
    } else  {
        return LVGL_ICON_STATUSBAR_BATTERY_ANDROID_FRAME_1;
    }
}

class StatusbarService final : public Service {

    Mutex mutex;
    std::unique_ptr<Timer> updateTimer;
    int8_t gps_icon_id;
    bool gps_last_state = false;
    int8_t bt_icon_id;
    const char* bt_last_icon = nullptr;
    int8_t wifi_icon_id;
    const char* wifi_last_icon = nullptr;
    int8_t sdcard_icon_id;
    const char* sdcard_last_icon = nullptr;
    int8_t power_icon_id;
    const char* power_last_icon = nullptr;
    int8_t usb_icon_id;
    bool usb_last_state = false;

    void lock() const {
        mutex.lock();
    }

    void unlock() const {
        mutex.unlock();
    }

    void updateGpsIcon() {
        auto gps_state = gps_service_get_state();
        bool show_icon = (gps_state == GpsServiceState::GPS_SERVICE_STATE_ON_PENDING) || (gps_state == GpsServiceState::GPS_SERVICE_STATE_ON);
        if (gps_last_state != show_icon) {
            if (show_icon) {
                lvgl::statusbar_icon_set_image(gps_icon_id, LVGL_ICON_STATUSBAR_LOCATION_ON);
                lvgl::statusbar_icon_set_visibility(gps_icon_id, true);
            } else {
                lvgl::statusbar_icon_set_visibility(gps_icon_id, false);
            }
            gps_last_state = show_icon;
        }
    }

    void updateBluetoothIcon() {
        auto radio_state = bluetooth::getRadioState();
        Device* btdev = device_find_first_active_by_type(&BLUETOOTH_TYPE);
        bool scanning = btdev ? bluetooth_is_scanning(btdev) : false;
        Device* serial_dev = bluetooth_serial_get_device();
        Device* midi_dev = bluetooth_midi_get_device();
        bool connected = (serial_dev && bluetooth_serial_is_connected(serial_dev)) ||
                         (midi_dev && bluetooth_midi_is_connected(midi_dev));
        const char* desired_icon = getBluetoothStatusIcon(radio_state, scanning, connected);
        if (bt_last_icon != desired_icon) {
            if (desired_icon != nullptr) {
                lvgl::statusbar_icon_set_image(bt_icon_id, desired_icon);
                lvgl::statusbar_icon_set_visibility(bt_icon_id, true);
            } else {
                lvgl::statusbar_icon_set_visibility(bt_icon_id, false);
            }
            bt_last_icon = desired_icon;
        }
    }

    void updateWifiIcon() {
        wifi::RadioState radio_state = wifi::getRadioState();
        const char* desired_icon = getWifiStatusIcon(radio_state);
        if (wifi_last_icon != desired_icon) {
            if (desired_icon != nullptr) {
                lvgl::statusbar_icon_set_image(wifi_icon_id, desired_icon);
                lvgl::statusbar_icon_set_visibility(wifi_icon_id, true);
            } else {
                lvgl::statusbar_icon_set_visibility(wifi_icon_id, false);
            }
            wifi_last_icon = desired_icon;
        }
    }

    void updatePowerStatusIcon() {
        const char* desired_icon = getPowerStatusIcon();
        if (power_last_icon != desired_icon) {
            if (desired_icon != nullptr) {
                lvgl::statusbar_icon_set_image(power_icon_id, desired_icon);
                lvgl::statusbar_icon_set_visibility(power_icon_id, true);
            } else {
                lvgl::statusbar_icon_set_visibility(power_icon_id, false);
            }
            power_last_icon = desired_icon;
        }
    }

    void updateUsbIcon() {
        Device* hid_dev  = device_find_first_active_by_type(&USB_HOST_HID_TYPE);
        Device* midi_dev = device_find_first_active_by_type(&USB_HOST_MIDI_TYPE);
        bool connected = (hid_dev && usb_host_hid_is_connected(hid_dev)) ||
                         (midi_dev && usb_midi_is_connected(midi_dev));
        if (!connected) {
            // MSC: scan filesystems for any mounted /usb* path
            file_system_for_each(&connected, [](struct FileSystem* fs, void* ctx) -> bool {
                if (!file_system_is_mounted(fs)) return true;
                char path[64];
                if (file_system_get_path(fs, path, sizeof(path)) == ERROR_NONE) {
                    if (strncmp(path, USB_MSC_MOUNT_PATH_PREFIX, sizeof(USB_MSC_MOUNT_PATH_PREFIX) - 1) == 0) {
                        *static_cast<bool*>(ctx) = true;
                        return false;
                    }
                }
                return true;
            });
        }
        if (connected != usb_last_state) {
            lvgl::statusbar_icon_set_visibility(usb_icon_id, connected);
            usb_last_state = connected;
        }
    }

    void updateSdCardIcon() {
        struct SdCardState { bool found; bool mounted; };
        SdCardState state = { false, false };

        file_system_for_each(&state, [](FileSystem* fs, void* context) {
            auto* s = static_cast<SdCardState*>(context);
            char path[64];
            if (file_system_get_path(fs, path, sizeof(path)) != ERROR_NONE) return true;
            if (strncmp(path, "/sdcard", 7) != 0) return true;
            s->found = true;
            s->mounted = file_system_is_mounted(fs);
            return false;
        });

        if (state.found) {
            auto* desired_icon = getSdCardStatusIcon(state.mounted);
            if (sdcard_last_icon != desired_icon) {
                lvgl::statusbar_icon_set_image(sdcard_icon_id, desired_icon);
                lvgl::statusbar_icon_set_visibility(sdcard_icon_id, true);
                sdcard_last_icon = desired_icon;
            }
        } else {
            if (sdcard_last_icon != nullptr) {
                lvgl::statusbar_icon_set_visibility(sdcard_icon_id, false);
                sdcard_last_icon = nullptr;
            }
        }
    }

    void update() {
        if (module_is_started(&lvgl_module)) {
            if (lvgl_try_lock(100)) {
                updateGpsIcon();
                updateBluetoothIcon();
                updateWifiIcon();
                updateSdCardIcon();
                updatePowerStatusIcon();
                updateUsbIcon();
                lvgl_unlock();
            }
        }
    }

public:

    StatusbarService() {
        gps_icon_id = lvgl::statusbar_icon_add();
        bt_icon_id = lvgl::statusbar_icon_add();
        usb_icon_id = lvgl::statusbar_icon_add();
        sdcard_icon_id = lvgl::statusbar_icon_add();
        wifi_icon_id = lvgl::statusbar_icon_add();
        power_icon_id = lvgl::statusbar_icon_add();
    }

    ~StatusbarService() override {
        lvgl::statusbar_icon_remove(power_icon_id);
        lvgl::statusbar_icon_remove(wifi_icon_id);
        lvgl::statusbar_icon_remove(sdcard_icon_id);
        lvgl::statusbar_icon_remove(usb_icon_id);
        lvgl::statusbar_icon_remove(bt_icon_id);
        lvgl::statusbar_icon_remove(gps_icon_id);
    }

    bool onStart(ServiceContext& serviceContext) override {
        if (lv_screen_active() == nullptr) {
            LOG_E(TAG, "No display found");
            return false;
        }

        // TODO: Make thread-safe for LVGL
        lvgl::statusbar_icon_set_visibility(wifi_icon_id, true);
        lvgl::statusbar_icon_set_image(usb_icon_id, LVGL_ICON_STATUSBAR_USB);
        lvgl::statusbar_icon_set_visibility(usb_icon_id, false);

        auto service = findServiceById<StatusbarService>(manifest.id);
        assert(service);
        service->update();

        updateTimer = std::make_unique<Timer>(Timer::Type::Periodic, pdMS_TO_TICKS(1000), [service] {
            service->update();
        });

        updateTimer->setCallbackPriority(Thread::Priority::Lower);

        // We want to try and scan more often in case of startup or scan lock failure
        updateTimer->start();

        return true;
    }

    void onStop(ServiceContext& service) override{
        updateTimer->stop();
        updateTimer = nullptr;
    }
};

extern const ServiceManifest manifest = {
    .id = "Statusbar",
    .createService = create<StatusbarService>
};

// endregion service

} // namespace
