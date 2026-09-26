// SPDX-License-Identifier: Apache-2.0
#include <wifi/wifi_autoconnect.h>
#include <wifi/wifi_settings.h>
#include <wifi/private/wifi_service.h>

#include <service/manifest.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/concurrent/thread.h>
#include <tactility/device.h>
#include <tactility/drivers/wifi.h>
#include <tactility/log.h>
#include <tactility/system_event.h>
#include <tactility/time.h>
#include <tactility/wifi_auto_scan.h>

#include <atomic>
#include <new>

constexpr auto* TAG = "wifi_autoconnect";
constexpr size_t SCAN_RECORD_LIMIT = 16;
constexpr size_t THREAD_STACK_SIZE = 5120;
constexpr TickType_t AUTO_SCAN_INTERVAL = pdMS_TO_TICKS(10000);
// Scan more often than AUTO_SCAN_INTERVAL in case of startup or scan lock failure
constexpr TickType_t AUTO_SCAN_POLL_INTERVAL = pdMS_TO_TICKS(2000);

// Set by wifi_autoconnect_pause_until_connected(), cleared by a connection result or the radio turning on.
// Distinct from external_scan_pause: the two must not clobber each other, otherwise a caller's explicit
// pause (e.g. AutoScanPauseGuard during a co-processor OTA) can be silently cleared by an unrelated
// connection attempt finishing.
static std::atomic<bool> pause_until_connected {false};
// Only set/cleared through wifi_auto_scan_set_paused()
static std::atomic<bool> external_scan_pause {false};

struct WifiAutoConnect {
    Device* device = nullptr;
    // All work (wifi events, periodic scans, boot) runs on this thread, so the ESP-IDF esp_event
    // task never runs any of it.
    Thread* thread = nullptr;
    std::atomic<bool> running {false};
    TaskEventGroup event_group {};
    WifiEventSubscription wifi_event_sub {};
    bool wifi_event_subscribed = false;
    uint32_t stop_bit = 0;
    uint32_t boot_completed_bit = 0;
    bool boot_event_subscribed = false;
    bool has_scanned = false;
    TickType_t last_scan_time = 0;
};

static bool is_radio_on(WifiAutoConnect* data) {
    WifiRadioState radio = WIFI_RADIO_STATE_OFF;
    return wifi_get_radio_state(data->device, &radio) == ERROR_NONE && radio == WIFI_RADIO_STATE_ON;
}

static bool is_station_active(WifiAutoConnect* data) {
    WifiStationState station = WIFI_STATION_STATE_DISCONNECTED;
    return wifi_get_station_state(data->device, &station) == ERROR_NONE && station != WIFI_STATION_STATE_DISCONNECTED;
}

static bool is_paused() {
    return pause_until_connected.load() || external_scan_pause.load();
}

static bool find_auto_connect_ap(WifiAutoConnect* data, WifiApSettings* out) {
    WifiApRecord records[SCAN_RECORD_LIMIT];
    size_t count = SCAN_RECORD_LIMIT;
    if (wifi_get_scan_results(data->device, records, &count) != ERROR_NONE) {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        if (!wifi_settings_contains(records[i].ssid)) {
            continue;
        }
        if (wifi_settings_load(records[i].ssid, out) != ERROR_NONE) {
            LOG_E(TAG, "Failed to load credentials for ssid %s", records[i].ssid);
        } else if (out->auto_connect) {
            return true;
        }
    }
    return false;
}

static void auto_connect(WifiAutoConnect* data) {
    // This runs on every SCAN_FINISHED, not just this service's own scans (e.g. WifiManage re-scans
    // on show), so it must honor the pauses. The radio-off check matters because a scan that was
    // already in flight can finish after the user turns the radio off.
    if (is_paused() || !is_radio_on(data)) {
        return;
    }
    // Already connected or connecting (including a manual attempt by an app): reconnecting would
    // override that attempt, or force a pointless disconnect/reconnect blip.
    if (is_station_active(data)) {
        return;
    }

    WifiApSettings target;
    if (find_auto_connect_ap(data, &target)) {
        LOG_I(TAG, "Auto-connecting to %s", target.ssid);
        error_t result = wifi_station_connect(data->device, target.ssid, target.password, target.channel);
        if (result != ERROR_NONE) {
            LOG_E(TAG, "Failed to auto-connect (%s)", error_to_string(result));
        }
    }
}

static void scan_if_needed(WifiAutoConnect* data) {
    if (is_paused() || !is_radio_on(data) || is_station_active(data) || wifi_is_scanning(data->device)) {
        return;
    }

    TickType_t now = get_ticks();
    if (data->has_scanned && (now - data->last_scan_time) < AUTO_SCAN_INTERVAL) {
        return;
    }

    data->has_scanned = true;
    data->last_scan_time = now;
    error_t result = wifi_scan(data->device);
    if (result != ERROR_NONE) {
        LOG_I(TAG, "Can't start scan (%s)", error_to_string(result));
    }
}

static void on_wifi_event(WifiAutoConnect* data, const WifiEvent& event) {
    switch (event.type) {
        case WIFI_EVENT_TYPE_RADIO_STATE_CHANGED:
            if (event.radio_state == WIFI_RADIO_STATE_ON) {
                // Resume auto-connect and scan right away
                pause_until_connected = false;
                data->has_scanned = false;
            }
            break;

        case WIFI_EVENT_TYPE_SCAN_FINISHED:
            auto_connect(data);
            break;

        case WIFI_EVENT_TYPE_STATION_STATE_CHANGED:
            if (event.station_state == WIFI_STATION_STATE_DISCONNECTED) {
                // Don't touch pause_until_connected here: a deliberate disconnect sets it and relies
                // on it staying set until a new connection is established.
                NetworkDisconnectedEvent disconnected_event = { .device = data->device };
                system_event_emit(KERNEL_EVENT_NETWORK_DISCONNECTED, &disconnected_event, sizeof(disconnected_event));
            }
            break;

        case WIFI_EVENT_TYPE_STATION_CONNECTION_RESULT:
            // A finished attempt (manual or automatic) ends the pause, so a failed manual attempt
            // lets auto-connect try other saved access points.
            pause_until_connected = false;
            break;

        default:
            break;
    }
}

static void on_boot_completed(WifiAutoConnect* data) {
    wifi_provisioning_import();

    if (wifi_settings_get_enable_on_boot()) {
        LOG_I(TAG, "Auto-enabling WiFi");
        if (wifi_set_radio_on(data->device) != ERROR_NONE) {
            LOG_E(TAG, "Failed to enable WiFi radio");
        }
    }
}

static int32_t thread_main(void* context) {
    auto* data = static_cast<WifiAutoConnect*>(context);
    while (data->running.load()) {
        uint32_t flags = 0;
        task_event_group_wait_any(&data->event_group, &flags, AUTO_SCAN_POLL_INTERVAL);
        if (!data->running.load()) {
            break;
        }

        if ((flags & data->boot_completed_bit) != 0) {
            on_boot_completed(data);
        }

        WifiEvent event {};
        while (wifi_event_poll(&data->wifi_event_sub, &event) == ERROR_NONE) {
            on_wifi_event(data, event);
        }

        scan_if_needed(data);
    }
    return 0;
}

static void on_boot_completed_event(SystemEvent* /*event*/, void* context) {
    auto* data = static_cast<WifiAutoConnect*>(context);
    task_event_group_signal(&data->event_group, data->boot_completed_bit);
}

static void set_external_scan_paused(bool paused) {
    LOG_I(TAG, "set_external_scan_paused(%d)", (int)paused);
    external_scan_pause = paused;
}

static void* create_service(const ServiceManifest* /*manifest*/) {
    return new (std::nothrow) WifiAutoConnect();
}

static void destroy_service(const ServiceManifest* /*manifest*/, void* data) {
    delete static_cast<WifiAutoConnect*>(data);
}

static void release_resources(WifiAutoConnect* data) {
    if (data->boot_event_subscribed) {
        system_event_callback_remove(KERNEL_EVENT_BOOT_COMPLETED, on_boot_completed_event);
        data->boot_event_subscribed = false;
    }
    if (data->wifi_event_subscribed) {
        wifi_event_unsubscribe(data->device, &data->wifi_event_sub);
        data->wifi_event_subscribed = false;
    }
    task_event_group_destruct(&data->event_group);
    device_stop(data->device);
    device_put(data->device);
    data->device = nullptr;
}

static error_t on_start(ServiceInstance* /*instance*/, void* context) {
    auto* data = static_cast<WifiAutoConnect*>(context);

    wifi_auto_scan_set_paused_function(set_external_scan_paused);

    Device* device = nullptr;
    if (device_get_first_by_type(&WIFI_TYPE, &device) != ERROR_NONE) {
        LOG_W(TAG, "No WiFi device found");
        return ERROR_NONE;
    }
    if (device_start(device) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start WiFi device");
        device_put(device);
        return ERROR_NONE;
    }
    data->device = device;

    task_event_group_construct(&data->event_group);
    if (task_event_group_claim_bit(&data->event_group, &data->stop_bit) != ERROR_NONE ||
        task_event_group_claim_bit(&data->event_group, &data->boot_completed_bit) != ERROR_NONE) {
        release_resources(data);
        return ERROR_RESOURCE;
    }

    if (wifi_event_subscribe(device, &data->wifi_event_sub, &data->event_group) == ERROR_NONE) {
        data->wifi_event_subscribed = true;
    } else {
        LOG_E(TAG, "Failed to subscribe to WiFi events");
    }

    if (system_event_callback_add(KERNEL_EVENT_BOOT_COMPLETED, on_boot_completed_event, data) == ERROR_NONE) {
        data->boot_event_subscribed = true;
    }

    data->running = true;
    data->thread = thread_alloc_full("wifi-autoconnect", THREAD_STACK_SIZE, thread_main, data, -1);
    if (data->thread == nullptr || thread_start(data->thread) != ERROR_NONE) {
        data->running = false;
        if (data->thread != nullptr) {
            thread_free(data->thread);
            data->thread = nullptr;
        }
        release_resources(data);
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

static void on_stop(ServiceInstance* /*instance*/, void* context) {
    auto* data = static_cast<WifiAutoConnect*>(context);

    if (data->device != nullptr) {
        data->running = false;
        task_event_group_signal(&data->event_group, data->stop_bit);
        thread_join(data->thread, portMAX_DELAY, pdMS_TO_TICKS(10));
        thread_free(data->thread);
        data->thread = nullptr;

        if (is_radio_on(data)) {
            wifi_set_radio_off(data->device);
        }
        release_resources(data);
    }

    pause_until_connected = false;
    wifi_auto_scan_set_paused_function(nullptr);
}

extern "C" {

void wifi_autoconnect_pause_until_connected(void) {
    LOG_I(TAG, "wifi_autoconnect_pause_until_connected()");
    pause_until_connected = true;
}

const ServiceManifest wifi_service_manifest = {
    .id = WIFI_SERVICE_ID,
    .create_service = create_service,
    .destroy_service = destroy_service,
    .on_start = on_start,
    .on_stop = on_stop,
};

}
