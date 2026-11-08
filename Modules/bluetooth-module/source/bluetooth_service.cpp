// SPDX-License-Identifier: Apache-2.0
#include <bluetooth/bluetooth_paired_device.h>
#include <bluetooth/bluetooth_service.h>
#include <bluetooth/bluetooth_settings.h>
#include <bluetooth/private/bluetooth_service.h>

#include <service/manifest.h>

#include <tactility/Memory.hpp>
#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/concurrent/thread.h>
#include <tactility/device.h>
#include <tactility/drivers/bluetooth.h>
#include <tactility/drivers/bluetooth_hid_device.h>
#include <tactility/drivers/bluetooth_midi.h>
#include <tactility/drivers/bluetooth_serial.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <tactility/std/vector.hpp>

#include <atomic>
#include <cstring>

constexpr auto* TAG = "bluetooth_service";

// File access happens on this thread
constexpr size_t THREAD_STACK_SIZE = 6144;
constexpr TickType_t THREAD_POLL_INTERVAL = pdMS_TO_TICKS(250);
// Scans repeat while the radio is on, so new devices show up without opening the app
constexpr TickType_t AUTO_SCAN_INTERVAL = pdMS_TO_TICKS(10000);

struct BluetoothService {
    Device* device = nullptr;
    Thread* thread = nullptr;
    std::atomic<bool> running {false};
    // Set when the radio was turned on through the service. Other apps can use the radio without the service interfering.
    std::atomic<bool> managed {false};
    TaskEventGroup event_group {};
    BtEventSubscription event_sub {};
    bool event_subscribed = false;
    uint32_t stop_bit = 0;

    // Guards the fields below
    Mutex mutex {};
    tt::std::vector<BluetoothPeer> scan_results;
    bool hid_host_connected = false;
    BtAddr hid_host_addr = {};

    // Only used by the service thread
    bool auto_scan_enabled = false;
    bool auto_scan_pending = false;
    TickType_t next_auto_scan_tick = 0;
};

static BluetoothService* service = nullptr;

static bool addr_equals(const BtAddr a, const BtAddr b) {
    return memcmp(a, b, BT_ADDR_LEN) == 0;
}

static void copy_name(char* destination, const char* source) {
    strncpy(destination, source, BT_NAME_MAX);
    destination[BT_NAME_MAX] = '\0';
}

// region Scan results

static void cache_peer(BluetoothService* data, const BtPeerRecord& record) {
    mutex_lock(&data->mutex);
    for (auto& existing : data->scan_results) {
        if (addr_equals(existing.addr, record.addr)) {
            if (record.name[0] != '\0') {
                copy_name(existing.name, record.name);
            }
            existing.rssi = record.rssi;
            mutex_unlock(&data->mutex);
            return;
        }
    }

    BluetoothPeer peer = {};
    memcpy(peer.addr, record.addr, BT_ADDR_LEN);
    copy_name(peer.name, record.name);
    peer.rssi = record.rssi;
    peer.paired = record.paired;
    peer.connected = record.connected;
    data->scan_results.push_back(peer);
    mutex_unlock(&data->mutex);
}

static void clear_scan_results(BluetoothService* data) {
    mutex_lock(&data->mutex);
    data->scan_results.clear();
    mutex_unlock(&data->mutex);
}

static bool find_scanned_name(BluetoothService* data, const BtAddr addr, char* out_name) {
    bool found = false;
    mutex_lock(&data->mutex);
    for (const auto& peer : data->scan_results) {
        if (addr_equals(peer.addr, addr)) {
            copy_name(out_name, peer.name);
            found = true;
            break;
        }
    }
    mutex_unlock(&data->mutex);
    return found;
}

// endregion

// region Auto-scan

static void schedule_auto_scan(BluetoothService* data, TickType_t delay) {
    data->auto_scan_pending = data->auto_scan_enabled;
    data->next_auto_scan_tick = get_ticks() + delay;
}

static void run_auto_scan(BluetoothService* data) {
    data->auto_scan_pending = false;
    if (!bluetooth_is_scanning(data->device) && bluetooth_scan_start(data->device) != ERROR_NONE) {
        schedule_auto_scan(data, AUTO_SCAN_INTERVAL);
    }
}

// endregion

// region Auto-connect

struct AutoConnectQuery {
    bool hid_host = false;
    bool hid_device = false;
};

static bool query_auto_connect(const BluetoothPairedDevice* device, void* context) {
    auto* query = static_cast<AutoConnectQuery*>(context);
    if (device->auto_connect) {
        query->hid_host |= device->profile == BT_PROFILE_HID_HOST;
        query->hid_device |= device->profile == BT_PROFILE_HID_DEVICE;
    }
    return true;
}

static AutoConnectQuery get_auto_connect_query() {
    AutoConnectQuery query;
    bluetooth_paired_device_for_each(&query, query_auto_connect);
    return query;
}

static void auto_start_profiles(BluetoothService* data) {
    AutoConnectQuery query = get_auto_connect_query();
    if (query.hid_host) {
        LOG_I(TAG, "HID host auto-connect peer found, starting scan");
        bluetooth_scan_start(data->device);
    } else {
        // HID device profiles are not auto-started. The identity, address and bond set belong
        // to the app that starts the profile, so a service start would expose the current
        // identity as a keyboard with no app behind it. A paired host reconnects once an app
        // starts the profile.
        if (bluetooth_settings_get_spp_auto_start()) {
            LOG_I(TAG, "Auto-starting SPP server");
            if (Device* serial = bluetooth_serial_get()) {
                bluetooth_serial_start(serial);
                device_put(serial);
            }
        }
        if (bluetooth_settings_get_midi_auto_start()) {
            LOG_I(TAG, "Auto-starting MIDI server");
            if (Device* midi = bluetooth_midi_get()) {
                bluetooth_midi_start(midi);
                device_put(midi);
            }
        }
    }
}

// Connects to the first saved HID host peer that appeared in the last scan.
// When none appeared, scans again so the search continues until the device powers on.
static void auto_connect_hid_host(BluetoothService* data) {
    // A peripheral profile owns the radio and its identity, and bonds are stored per identity,
    // so a host connect here would authenticate against the wrong bond set.
    if (bluetooth_is_peripheral_active(data->device)) {
        return;
    }

    mutex_lock(&data->mutex);
    bool connected = data->hid_host_connected;
    tt::std::vector<BluetoothPeer> scanned = data->scan_results;
    mutex_unlock(&data->mutex);
    if (connected) {
        return;
    }

    for (const auto& peer : scanned) {
        BluetoothPairedDevice stored;
        if (bluetooth_paired_device_load(peer.addr, &stored) == ERROR_NONE && stored.auto_connect && stored.profile == BT_PROFILE_HID_HOST) {
            char addr_hex[BLUETOOTH_ADDR_HEX_LENGTH + 1];
            bluetooth_addr_to_hex(peer.addr, addr_hex);
            LOG_I(TAG, "Auto-connecting HID host to %s", addr_hex);
            bluetooth_host_connect(data->device, peer.addr);
            return;
        }
    }

    if (get_auto_connect_query().hid_host && !bluetooth_is_scanning(data->device)) {
        LOG_I(TAG, "Auto-connect HID host: device not in scan, retrying scan");
        bluetooth_scan_start(data->device);
    }
}

// endregion

// region Events

static void save_connected_peer(BluetoothService* data, const BtProfileStateData& state) {
    BluetoothPairedDevice stored;
    bool exists = bluetooth_paired_device_load(state.addr, &stored) == ERROR_NONE;

    if (state.profile == BT_PROFILE_HID_HOST) {
        // A HID host peer is saved once its connection is ready, because it needs no separate pairing step
        BluetoothPairedDevice device = {};
        memcpy(device.addr, state.addr, BT_ADDR_LEN);
        device.auto_connect = exists ? stored.auto_connect : true;
        device.profile = BT_PROFILE_HID_HOST;
        if (!find_scanned_name(data, state.addr, device.name) && exists) {
            copy_name(device.name, stored.name);
        }
        bluetooth_paired_device_save(&device);
    } else if (exists && stored.profile != static_cast<int>(state.profile)) {
        stored.profile = static_cast<int>(state.profile);
        bluetooth_paired_device_save(&stored);
    }
}

static void on_profile_state_changed(BluetoothService* data, const BtProfileStateData& state) {
    if (state.profile == BT_PROFILE_HID_HOST) {
        mutex_lock(&data->mutex);
        data->hid_host_connected = state.state == BT_PROFILE_STATE_CONNECTED;
        if (data->hid_host_connected) {
            memcpy(data->hid_host_addr, state.addr, BT_ADDR_LEN);
        }
        mutex_unlock(&data->mutex);
    }

    if (state.state == BT_PROFILE_STATE_CONNECTED) {
        save_connected_peer(data, state);
    } else if (state.state == BT_PROFILE_STATE_IDLE && state.profile == BT_PROFILE_HID_HOST) {
        // Scan again when a peer wants to be auto-connected, so auto_connect_hid_host() runs when the scan finishes
        if (data->managed && get_auto_connect_query().hid_host && !bluetooth_is_scanning(data->device)) {
            bluetooth_scan_start(data->device);
        }
    }
}

static void on_pair_result(const BtPairResultData& result) {
    if (result.result == BT_PAIR_RESULT_SUCCESS) {
        if (!bluetooth_paired_device_exists(result.addr)) {
            BluetoothPairedDevice device = {};
            memcpy(device.addr, result.addr, BT_ADDR_LEN);
            device.auto_connect = true;
            device.profile = result.profile;
            bluetooth_paired_device_save(&device);
        }
    } else if (result.result == BT_PAIR_RESULT_BOND_LOST) {
        bluetooth_paired_device_remove(result.addr);
    }
}

static void on_bluetooth_event(BluetoothService* data, const BtEvent& event) {
    switch (event.type) {
        case BT_EVENT_RADIO_STATE_CHANGED:
            data->auto_scan_enabled = event.radio_state == BT_RADIO_STATE_ON && data->managed;
            schedule_auto_scan(data, 0);
            if (event.radio_state == BT_RADIO_STATE_ON && data->managed) {
                auto_start_profiles(data);
            } else if (event.radio_state == BT_RADIO_STATE_OFF) {
                // The event can be older than a request to turn the radio back on, which must stay managed
                BtRadioState current = BT_RADIO_STATE_OFF;
                if (bluetooth_get_radio_state(data->device, &current) != ERROR_NONE || current == BT_RADIO_STATE_OFF) {
                    data->managed = false;
                }
                clear_scan_results(data);
            }
            break;

        case BT_EVENT_SCAN_STARTED:
            clear_scan_results(data);
            break;

        case BT_EVENT_SCAN_FINISHED:
            schedule_auto_scan(data, AUTO_SCAN_INTERVAL);
            if (data->managed) {
                auto_connect_hid_host(data);
            }
            break;

        case BT_EVENT_PEER_FOUND:
            cache_peer(data, event.peer);
            break;

        case BT_EVENT_PAIR_RESULT:
            on_pair_result(event.pair_result);
            break;

        case BT_EVENT_PROFILE_STATE_CHANGED:
            on_profile_state_changed(data, event.profile_state);
            break;

        default:
            break;
    }
}

// endregion

// region Service

static int32_t thread_main(void* context) {
    auto* data = static_cast<BluetoothService*>(context);
    while (data->running.load()) {
        task_event_group_wait_any(&data->event_group, nullptr, THREAD_POLL_INTERVAL);
        if (!data->running.load()) {
            break;
        }

        BtEvent event {};
        while (data->event_subscribed && bluetooth_event_poll(&data->event_sub, &event) == ERROR_NONE) {
            on_bluetooth_event(data, event);
        }

        if (data->auto_scan_pending && static_cast<int32_t>(get_ticks() - data->next_auto_scan_tick) >= 0) {
            run_auto_scan(data);
        }
    }
    return 0;
}

static void* create_service(const ServiceManifest* /*manifest*/) {
    auto* data = tt::newObject<BluetoothService>();
    if (data != nullptr) {
        mutex_construct(&data->mutex);
    }
    return data;
}

static void destroy_service(const ServiceManifest* /*manifest*/, void* context) {
    auto* data = static_cast<BluetoothService*>(context);
    mutex_destruct(&data->mutex);
    tt::deleteObject(data);
}

static void release_resources(BluetoothService* data) {
    if (data->event_subscribed) {
        bluetooth_event_unsubscribe(data->device, &data->event_sub);
        data->event_subscribed = false;
    }
    task_event_group_destruct(&data->event_group);
    device_put(data->device);
    data->device = nullptr;
}

static error_t on_start(ServiceInstance* /*instance*/, void* context) {
    auto* data = static_cast<BluetoothService*>(context);

    Device* device = nullptr;
    if (device_get_first_active_by_type(&BLUETOOTH_TYPE, &device) != ERROR_NONE) {
        LOG_W(TAG, "No Bluetooth device found");
        return ERROR_NONE;
    }
    data->device = device;

    task_event_group_construct(&data->event_group);
    if (task_event_group_claim_bit(&data->event_group, &data->stop_bit) != ERROR_NONE) {
        release_resources(data);
        return ERROR_RESOURCE;
    }

    if (bluetooth_event_subscribe(device, &data->event_sub, &data->event_group) == ERROR_NONE) {
        data->event_subscribed = true;
    } else {
        LOG_E(TAG, "Failed to subscribe to Bluetooth events");
    }

    // Runs here instead of after boot, so the radio comes up before Wi-Fi does. Both bring up the same co-processor
    // transport on boards with ESP-Hosted, and doing it concurrently makes both fail.
    if (bluetooth_settings_get_enable()) {
        LOG_I(TAG, "Auto-enabling Bluetooth");
        data->managed = true;
        if (bluetooth_set_radio_enabled(device, true) != ERROR_NONE) {
            LOG_E(TAG, "Failed to enable Bluetooth radio");
        }
    }

    data->running = true;
    data->thread = thread_alloc_full("bluetooth", THREAD_STACK_SIZE, thread_main, data, -1);
    if (data->thread == nullptr || thread_start(data->thread) != ERROR_NONE) {
        data->running = false;
        if (data->thread != nullptr) {
            thread_free(data->thread);
            data->thread = nullptr;
        }
        release_resources(data);
        return ERROR_RESOURCE;
    }

    service = data;
    return ERROR_NONE;
}

static void on_stop(ServiceInstance* /*instance*/, void* context) {
    auto* data = static_cast<BluetoothService*>(context);
    if (data->device == nullptr) {
        return;
    }

    service = nullptr;
    data->running = false;
    task_event_group_signal(&data->event_group, data->stop_bit);
    thread_join(data->thread, portMAX_DELAY, pdMS_TO_TICKS(10));
    thread_free(data->thread);
    data->thread = nullptr;
    release_resources(data);
}

// endregion

// region Public API

static BluetoothPeer peer_from_paired_device(const BluetoothPairedDevice& device) {
    BluetoothPeer peer = {};
    memcpy(peer.addr, device.addr, BT_ADDR_LEN);
    copy_name(peer.name, device.name);
    peer.paired = true;
    peer.profile = device.profile;
    return peer;
}

struct PairedPeerQuery {
    void* context;
    bool (*on_peer)(const BluetoothPeer* peer, void* context);
    bool hid_host_connected;
    const uint8_t* hid_host_addr;
    bool hid_host_listed;
    bool stopped;
};

static bool on_paired_device(const BluetoothPairedDevice* device, void* context) {
    auto* query = static_cast<PairedPeerQuery*>(context);
    BluetoothPeer peer = peer_from_paired_device(*device);
    if (query->hid_host_connected && addr_equals(peer.addr, query->hid_host_addr)) {
        peer.connected = true;
        query->hid_host_listed = true;
    }
    query->stopped = !query->on_peer(&peer, query->context);
    return !query->stopped;
}

extern "C" {

const ServiceManifest bluetooth_service_manifest = {
    .id = BLUETOOTH_SERVICE_ID,
    .create_service = create_service,
    .destroy_service = destroy_service,
    .on_start = on_start,
    .on_stop = on_stop,
};

void bluetooth_service_for_each_scan_result(void* context, bool (*on_peer)(const BluetoothPeer* peer, void* context)) {
    if (service == nullptr) {
        return;
    }
    mutex_lock(&service->mutex);
    for (const auto& peer : service->scan_results) {
        if (!on_peer(&peer, context)) {
            break;
        }
    }
    mutex_unlock(&service->mutex);
}

void bluetooth_service_for_each_paired_peer(void* context, bool (*on_peer)(const BluetoothPeer* peer, void* context)) {
    if (service == nullptr) {
        return;
    }

    PairedPeerQuery query = {
        .context = context,
        .on_peer = on_peer,
        .hid_host_connected = false,
        .hid_host_addr = nullptr,
        .hid_host_listed = false,
        .stopped = false,
    };
    BtAddr hid_host_addr;
    mutex_lock(&service->mutex);
    query.hid_host_connected = service->hid_host_connected;
    memcpy(hid_host_addr, service->hid_host_addr, BT_ADDR_LEN);
    mutex_unlock(&service->mutex);
    query.hid_host_addr = hid_host_addr;

    bluetooth_paired_device_for_each(&query, on_paired_device);

    // The file system can lag behind a save, so a connected peer is listed even when its file isn't found yet
    if (query.hid_host_connected && !query.hid_host_listed && !query.stopped) {
        BluetoothPeer peer = {};
        memcpy(peer.addr, hid_host_addr, BT_ADDR_LEN);
        find_scanned_name(service, hid_host_addr, peer.name);
        peer.paired = true;
        peer.connected = true;
        peer.profile = BT_PROFILE_HID_HOST;
        on_peer(&peer, context);
    }
}

error_t bluetooth_service_set_radio_enabled(bool enabled) {
    if (service == nullptr) {
        return ERROR_INVALID_STATE;
    }
    if (enabled) {
        service->managed = true;
    }
    return bluetooth_set_radio_enabled(service->device, enabled);
}

void bluetooth_service_connect(const BtAddr addr, enum BtProfileId profile) {
    if (service == nullptr) {
        return;
    }
    LOG_I(TAG, "connect(profile=%d)", static_cast<int>(profile));
    switch (profile) {
        case BT_PROFILE_HID_HOST:
            bluetooth_host_connect(service->device, addr);
            break;
        case BT_PROFILE_HID_DEVICE:
            if (Device* hid_device = bluetooth_hid_device_get()) {
                bluetooth_hid_device_start(hid_device, BT_HID_DEVICE_MODE_KEYBOARD);
                device_put(hid_device);
            }
            break;
        case BT_PROFILE_SPP:
            if (Device* serial = bluetooth_serial_get()) {
                bluetooth_serial_start(serial);
                bluetooth_settings_set_spp_auto_start(true);
                device_put(serial);
            }
            break;
        case BT_PROFILE_MIDI:
            if (Device* midi = bluetooth_midi_get()) {
                bluetooth_midi_start(midi);
                bluetooth_settings_set_midi_auto_start(true);
                device_put(midi);
            }
            break;
    }
}

void bluetooth_service_disconnect(const BtAddr addr, enum BtProfileId profile) {
    if (service == nullptr) {
        return;
    }
    LOG_I(TAG, "disconnect(profile=%d)", static_cast<int>(profile));
    switch (profile) {
        case BT_PROFILE_HID_HOST:
            bluetooth_host_disconnect(service->device, addr);
            break;
        case BT_PROFILE_HID_DEVICE:
            if (Device* hid_device = bluetooth_hid_device_get()) {
                bluetooth_hid_device_stop(hid_device);
                device_put(hid_device);
            }
            break;
        case BT_PROFILE_SPP:
            if (Device* serial = bluetooth_serial_get()) {
                bluetooth_serial_stop(serial);
                device_put(serial);
            }
            break;
        case BT_PROFILE_MIDI:
            if (Device* midi = bluetooth_midi_get()) {
                bluetooth_midi_stop(midi);
                device_put(midi);
            }
            break;
    }
}

void bluetooth_service_unpair(const BtAddr addr) {
    if (service == nullptr) {
        return;
    }
    bluetooth_unpair(service->device, addr);
    bluetooth_paired_device_remove(addr);
}

}

// endregion
