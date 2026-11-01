#include <lvgl/widgets/statusbar.h>

#include <Tactility/Mutex.h>
#include <Tactility/Timer.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/ServicePaths.h>
#include <Tactility/service/ServiceRegistration.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/bluetooth.h>
#include <tactility/drivers/bluetooth_hid_device.h>
#include <tactility/drivers/bluetooth_midi.h>
#include <tactility/drivers/bluetooth_serial.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/drivers/usb_host_hid.h>
#include <tactility/drivers/usb_host_midi.h>
#include <tactility/drivers/usb_host_msc.h>
#include <tactility/drivers/wifi.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/log.h>

#include <lvgl/icons/statusbar.h>
#include <lvgl/lvgl.h>

#include <app/install.h>
#include <app_hub/app_hub.h>

#include <cstring>
#include <set>
#include <string>

#include <location/location.h>

#include <mesh/mesh.h>

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

static const char* getWifiStatusIcon() {
    Device* device = nullptr;
    if (device_get_first_by_type(&WIFI_TYPE, &device) != ERROR_NONE) {
        return LVGL_ICON_STATUSBAR_SIGNAL_WIFI_OFF;
    }

    WifiRadioState radio_state = WIFI_RADIO_STATE_OFF;
    WifiStationState station_state = WIFI_STATION_STATE_DISCONNECTED;
    int32_t rssi = 1;
    wifi_get_radio_state(device, &radio_state);
    wifi_get_station_state(device, &station_state);
    if (station_state == WIFI_STATION_STATE_CONNECTED) {
        wifi_station_get_rssi(device, &rssi);
    }
    device_put(device);

    switch (radio_state) {
        case WIFI_RADIO_STATE_OFF:
        case WIFI_RADIO_STATE_OFF_PENDING:
            return LVGL_ICON_STATUSBAR_SIGNAL_WIFI_OFF;
        case WIFI_RADIO_STATE_ON_PENDING:
            return LVGL_ICON_STATUSBAR_SIGNAL_WIFI_0_BAR;
        case WIFI_RADIO_STATE_ON:
            if (station_state == WIFI_STATION_STATE_CONNECTED) {
                return getWifiStatusIconForRssi(rssi);
            }
            return LVGL_ICON_STATUSBAR_SIGNAL_WIFI_0_BAR;
    }
    check(false, "not implemented");
}

// Only shown while the radio is on
static const char* getBluetoothStatusIcon(BtRadioState state, bool scanning, bool connected) {
    switch (state) {
        case BT_RADIO_STATE_OFF:
        case BT_RADIO_STATE_OFF_PENDING:
            return nullptr;
        case BT_RADIO_STATE_ON_PENDING:
        case BT_RADIO_STATE_ON:
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
    const char* gps_last_icon = nullptr;
    int8_t mesh_icon_id;
    bool mesh_last_state = false;
    int8_t bt_icon_id;
    const char* bt_last_icon = nullptr;
    int8_t wifi_icon_id;
    const char* wifi_last_icon = nullptr;
    int8_t power_icon_id;
    const char* power_last_icon = nullptr;
    int8_t usb_icon_id;
    bool usb_last_state = false;
    int8_t install_icon_id;
    bool install_last_state = false;
    /** The number of app_install() calls seen by the previous update, to show installs that ended between two updates */
    uint32_t install_seen_count = app_install_get_started_count();
    TaskEventGroup appHubEventGroup {};
    AppHubEventSubscription appHubSubscription {};
    bool appHubSubscribed = false;
    /** The apps that App Hub is installing */
    std::set<std::string> installingApps;

    void lock() const {
        mutex.lock();
    }

    void unlock() const {
        mutex.unlock();
    }

    void updateGpsIcon() {
        // Only shown while location is being used
        const char* icon = location_is_in_use() ? LVGL_ICON_STATUSBAR_LOCATION_ON : nullptr;
        if (gps_last_icon != icon) {
            if (icon != nullptr) {
                lvgl_statusbar_icon_set_image(gps_icon_id, icon);
                lvgl_statusbar_icon_set_visibility(gps_icon_id, true);
            } else {
                lvgl_statusbar_icon_set_visibility(gps_icon_id, false);
            }
            gps_last_icon = icon;
        }
    }

    void updateMeshIcon() {
        const bool active = mesh_get_state() == MESH_STATE_ON;
        if (active != mesh_last_state) {
            lvgl_statusbar_icon_set_visibility(mesh_icon_id, active);
            mesh_last_state = active;
        }
    }

    void updateBluetoothIcon() {
        BtRadioState radio_state = BT_RADIO_STATE_OFF;
        bool scanning;

        {
            Device* btdev;
            if (device_get_first_active_by_type(&BLUETOOTH_TYPE, &btdev) == ERROR_NONE) {
                bluetooth_get_radio_state(btdev, &radio_state);
                scanning = bluetooth_is_scanning(btdev);
                device_put(btdev);
            } else {
                scanning = false;
            }
        }

        Device* serial_dev = bluetooth_serial_get();
        Device* midi_dev = bluetooth_midi_get();
        Device* hid_dev = bluetooth_hid_device_get();
        bool connected = (serial_dev && bluetooth_serial_is_connected(serial_dev)) ||
                         (midi_dev && bluetooth_midi_is_connected(midi_dev)) ||
                         (hid_dev && bluetooth_hid_device_is_connected(hid_dev));
        if (serial_dev) {
            device_put(serial_dev);
        }
        if (midi_dev) {
            device_put(midi_dev);
        }
        if (hid_dev) {
            device_put(hid_dev);
        }
        const char* desired_icon = getBluetoothStatusIcon(radio_state, scanning, connected);
        if (bt_last_icon != desired_icon) {
            if (desired_icon != nullptr) {
                lvgl_statusbar_icon_set_image(bt_icon_id, desired_icon);
                lvgl_statusbar_icon_set_visibility(bt_icon_id, true);
            } else {
                lvgl_statusbar_icon_set_visibility(bt_icon_id, false);
            }
            bt_last_icon = desired_icon;
        }
    }

    void updateWifiIcon() {
        const char* desired_icon = getWifiStatusIcon();
        if (wifi_last_icon != desired_icon) {
            if (desired_icon != nullptr) {
                lvgl_statusbar_icon_set_image(wifi_icon_id, desired_icon);
                lvgl_statusbar_icon_set_visibility(wifi_icon_id, true);
            } else {
                lvgl_statusbar_icon_set_visibility(wifi_icon_id, false);
            }
            wifi_last_icon = desired_icon;
        }
    }

    void updatePowerStatusIcon() {
        const char* desired_icon = getPowerStatusIcon();
        if (power_last_icon != desired_icon) {
            if (desired_icon != nullptr) {
                lvgl_statusbar_icon_set_image(power_icon_id, desired_icon);
                lvgl_statusbar_icon_set_visibility(power_icon_id, true);
            } else {
                lvgl_statusbar_icon_set_visibility(power_icon_id, false);
            }
            power_last_icon = desired_icon;
        }
    }

    static bool isHidOrMidiConnected() {
        Device* hid_dev = nullptr;
        device_get_first_active_by_type(&USB_HOST_HID_TYPE, &hid_dev);
        Device* midi_dev = nullptr;
        device_get_first_active_by_type(&USB_HOST_MIDI_TYPE, &midi_dev);

        // These devices only exist while connected
        bool connected = hid_dev != nullptr || midi_dev != nullptr;

        if (hid_dev) {
            device_put(hid_dev);
        }
        if (midi_dev) {
            device_put(midi_dev);
        }

        return connected;
    }

    void updateUsbIcon() {
        bool connected = isHidOrMidiConnected();
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
            lvgl_statusbar_icon_set_visibility(usb_icon_id, connected);
            usb_last_state = connected;
        }
    }

    void updateInstallIcon() {
        AppHubEvent event {};
        while (appHubSubscribed && app_hub_event_poll(&appHubSubscription, &event) == ERROR_NONE) {
            if (event.type == APP_HUB_EVENT_INSTALLING) {
                installingApps.insert(event.app_id);
            } else if (event.type == APP_HUB_EVENT_INSTALLED) {
                installingApps.erase(event.app_id);
            }
        }
        const uint32_t startedCount = app_install_get_started_count();
        const bool startedSinceLastUpdate = startedCount != install_seen_count;
        install_seen_count = startedCount;
        const bool installing = !installingApps.empty() || app_install_is_active() || startedSinceLastUpdate;
        if (installing != install_last_state) {
            lvgl_statusbar_icon_set_visibility(install_icon_id, installing);
            install_last_state = installing;
        }
    }

    void update() {
        if (lvgl_is_running()) {
            if (lvgl_try_lock(200)) {
                updateGpsIcon();
                updateMeshIcon();
                updateBluetoothIcon();
                updateWifiIcon();
                updatePowerStatusIcon();
                updateUsbIcon();
                updateInstallIcon();
                lvgl_unlock();
            }
        }
    }

public:

    StatusbarService() {
        gps_icon_id = lvgl_statusbar_icon_add(nullptr, false);
        // Added before the mesh icon, as the side strip hides the last added icons first when they don't fit
        install_icon_id = lvgl_statusbar_icon_add(LVGL_ICON_STATUSBAR_DOWNLOAD, false);
        mesh_icon_id = lvgl_statusbar_icon_add(nullptr, false);
        bt_icon_id = lvgl_statusbar_icon_add(nullptr, false);
        usb_icon_id = lvgl_statusbar_icon_add(nullptr, false);
        wifi_icon_id = lvgl_statusbar_icon_add(nullptr, false);
        power_icon_id = lvgl_statusbar_icon_add(nullptr, false);
    }

    ~StatusbarService() override {
        lvgl_statusbar_icon_remove(power_icon_id);
        lvgl_statusbar_icon_remove(wifi_icon_id);
        lvgl_statusbar_icon_remove(usb_icon_id);
        lvgl_statusbar_icon_remove(bt_icon_id);
        lvgl_statusbar_icon_remove(mesh_icon_id);
        lvgl_statusbar_icon_remove(install_icon_id);
        lvgl_statusbar_icon_remove(gps_icon_id);
    }

    bool onStart(ServiceContext& serviceContext) override {
        if (lv_screen_active() == nullptr) {
            LOG_E(TAG, "No display found");
            return false;
        }

        // TODO: Make thread-safe for LVGL
        lvgl_statusbar_icon_set_visibility(wifi_icon_id, true);
        lvgl_statusbar_icon_set_image(usb_icon_id, LVGL_ICON_STATUSBAR_USB);
        lvgl_statusbar_icon_set_visibility(usb_icon_id, false);
        lvgl_statusbar_icon_set_image(mesh_icon_id, LVGL_ICON_STATUSBAR_HUB);
        lvgl_statusbar_icon_set_visibility(mesh_icon_id, false);

        task_event_group_construct(&appHubEventGroup);
        appHubSubscribed = app_hub_event_subscribe(&appHubSubscription, &appHubEventGroup) == ERROR_NONE;
        if (!appHubSubscribed) {
            LOG_E(TAG, "Failed to subscribe to App Hub events");
        }

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
        if (appHubSubscribed) {
            app_hub_event_unsubscribe(&appHubSubscription);
            appHubSubscribed = false;
        }
        installingApps.clear();
        task_event_group_destruct(&appHubEventGroup);
    }
};

extern const ServiceManifest manifest = {
    .id = "tactility.statusbar",
    .createService = create<StatusbarService>
};

// endregion service

} // namespace
