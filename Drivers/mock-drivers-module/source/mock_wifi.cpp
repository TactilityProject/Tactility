// SPDX-License-Identifier: Apache-2.0

// Mock WiFi driver for the simulator: fakes a radio with a fixed list of
// access points instead of talking to real hardware. Behaviour is modeled
// after Tactility/Source/service/wifi/WifiMock.cpp (the old HAL's equivalent
// mock) - actions succeed instantly, there's no real scan or connect delay.

#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/wifi.h>
#include <tactility/log.h>

#include <algorithm>
#include <cstring>
#include <new>

#define TAG "mock_wifi"

namespace {

struct MockApRecord {
    const char* ssid;
    int8_t rssi;
    WifiAuthenticationType authentication_type;
};

// Same fixture data as the old WifiMock.cpp, so simulator UI testing sees
// familiar results.
constexpr MockApRecord MOCK_SCAN_RESULTS[] = {
    { "Home Wifi", -30, WIFI_AUTHENTICATION_TYPE_WPA2_PSK },
    { "No place like 127.0.0.1", -67, WIFI_AUTHENTICATION_TYPE_WPA2_PSK },
    { "Pretty fly for a Wi-Fi", -70, WIFI_AUTHENTICATION_TYPE_WPA2_PSK },
    { "An AP with a really, really long name", -80, WIFI_AUTHENTICATION_TYPE_WPA2_PSK },
    { "Bad Reception", -90, WIFI_AUTHENTICATION_TYPE_OPEN },
};
constexpr size_t MOCK_SCAN_RESULT_COUNT = sizeof(MOCK_SCAN_RESULTS) / sizeof(MOCK_SCAN_RESULTS[0]);
constexpr int8_t MOCK_CONNECTED_RSSI = -30;
constexpr const char* MOCK_IPV4_ADDRESS = "192.168.1.2";
constexpr const char* MOCK_ACCESS_POINT_IPV4_ADDRESS = "192.168.4.1";
constexpr uint8_t MOCK_ACCESS_POINT_MAX_CONNECTIONS = 15;

struct MockWifiCtx {
    Device* device = nullptr;

    Mutex mutex {};
    WifiRadioState radioState = WIFI_RADIO_STATE_OFF;
    WifiStationState stationState = WIFI_STATION_STATE_DISCONNECTED;
    WifiAccessPointState accessPointState = WIFI_ACCESS_POINT_STATE_STOPPED;
    bool scanning = false;
    char targetSsid[33] = {};

    Mutex subscriptionsMutex {};
    WifiEventSubscription* subscriptions = nullptr;
};

#define GET_CTX(device) (static_cast<MockWifiCtx*>(device_get_driver_data(device)))

void fireEvent(MockWifiCtx* ctx, WifiEvent event) {
    mutex_lock(&ctx->subscriptionsMutex);
    for (WifiEventSubscription* sub = ctx->subscriptions; sub != nullptr; sub = sub->internal.next) {
        mutex_lock(&sub->internal.ring_mutex);
        if (sub->internal.count < WIFI_EVENT_QUEUE_CAPACITY) {
            uint8_t tail = (sub->internal.head + sub->internal.count) % WIFI_EVENT_QUEUE_CAPACITY;
            sub->internal.queue[tail] = event;
            sub->internal.count++;
        }
        mutex_unlock(&sub->internal.ring_mutex);
        task_event_group_signal(sub->internal.event_group, sub->bit);
    }
    mutex_unlock(&ctx->subscriptionsMutex);
}

// ---- WifiApi ----

error_t apiGetRadioState(Device* device, WifiRadioState* state) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || state == nullptr) return ERROR_INVALID_ARGUMENT;
    mutex_lock(&ctx->mutex);
    *state = ctx->radioState;
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

error_t apiGetStationState(Device* device, WifiStationState* state) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || state == nullptr) return ERROR_INVALID_ARGUMENT;
    mutex_lock(&ctx->mutex);
    *state = ctx->stationState;
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

error_t apiGetAccessPointState(Device* device, WifiAccessPointState* state) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || state == nullptr) return ERROR_INVALID_ARGUMENT;
    mutex_lock(&ctx->mutex);
    *state = ctx->accessPointState;
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

void setAccessPointState(MockWifiCtx* ctx, WifiAccessPointState state) {
    mutex_lock(&ctx->mutex);
    ctx->accessPointState = state;
    mutex_unlock(&ctx->mutex);

    WifiEvent event = {};
    event.type = WIFI_EVENT_TYPE_ACCESS_POINT_STATE_CHANGED;
    event.access_point_state = state;
    fireEvent(ctx, event);
}

error_t apiAccessPointStart(Device* device, const WifiAccessPointConfig* config) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;
    if (config == nullptr) return ERROR_INVALID_ARGUMENT;

    const size_t ssidLength = strnlen(config->ssid, sizeof(config->ssid));
    const size_t passwordLength = strnlen(config->password, sizeof(config->password));
    const bool ssidValid = ssidLength > 0 && ssidLength < sizeof(config->ssid);
    const bool passwordValid = passwordLength == 0 || (passwordLength >= 8 && passwordLength < sizeof(config->password));
    if (!ssidValid || !passwordValid || config->channel > 13 || config->max_connections > MOCK_ACCESS_POINT_MAX_CONNECTIONS) {
        return ERROR_INVALID_ARGUMENT;
    }

    mutex_lock(&ctx->mutex);
    const bool started = ctx->accessPointState == WIFI_ACCESS_POINT_STATE_STARTED;
    mutex_unlock(&ctx->mutex);
    if (started) return ERROR_INVALID_STATE;

    setAccessPointState(ctx, WIFI_ACCESS_POINT_STATE_STARTED);
    return ERROR_NONE;
}

error_t apiAccessPointStop(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;

    mutex_lock(&ctx->mutex);
    const bool started = ctx->accessPointState == WIFI_ACCESS_POINT_STATE_STARTED;
    mutex_unlock(&ctx->mutex);
    if (started) {
        setAccessPointState(ctx, WIFI_ACCESS_POINT_STATE_STOPPED);
    }
    return ERROR_NONE;
}

error_t apiAccessPointGetIpv4Address(Device* device, char* ipv4) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ipv4 == nullptr) return ERROR_INVALID_ARGUMENT;

    mutex_lock(&ctx->mutex);
    const bool started = ctx->accessPointState == WIFI_ACCESS_POINT_STATE_STARTED;
    mutex_unlock(&ctx->mutex);
    if (!started) return ERROR_INVALID_STATE;

    strcpy(ipv4, MOCK_ACCESS_POINT_IPV4_ADDRESS);
    return ERROR_NONE;
}

bool apiIsScanning(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return false;
    mutex_lock(&ctx->mutex);
    bool scanning = ctx->scanning;
    mutex_unlock(&ctx->mutex);
    return scanning;
}

error_t apiScan(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;

    mutex_lock(&ctx->mutex);
    if (ctx->radioState != WIFI_RADIO_STATE_ON || ctx->scanning) {
        mutex_unlock(&ctx->mutex);
        return ERROR_INVALID_STATE;
    }
    ctx->scanning = true;
    mutex_unlock(&ctx->mutex);

    WifiEvent started_event = {};
    started_event.type = WIFI_EVENT_TYPE_SCAN_STARTED;
    fireEvent(ctx, started_event);

    // No real radio, so the "scan" completes instantly.
    mutex_lock(&ctx->mutex);
    ctx->scanning = false;
    mutex_unlock(&ctx->mutex);

    WifiEvent finished_event = {};
    finished_event.type = WIFI_EVENT_TYPE_SCAN_FINISHED;
    fireEvent(ctx, finished_event);

    return ERROR_NONE;
}

error_t apiGetScanResults(Device* device, WifiApRecord* results, size_t* num_results) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || results == nullptr || num_results == nullptr) return ERROR_INVALID_ARGUMENT;

    size_t count = std::min(*num_results, MOCK_SCAN_RESULT_COUNT);
    for (size_t i = 0; i < count; i++) {
        const MockApRecord& src = MOCK_SCAN_RESULTS[i];
        WifiApRecord& dst = results[i];
        memset(dst.ssid, 0, sizeof(dst.ssid));
        strncpy(dst.ssid, src.ssid, sizeof(dst.ssid) - 1);
        dst.rssi = src.rssi;
        dst.channel = 1;
        dst.authentication_type = src.authentication_type;
    }
    *num_results = count;
    return ERROR_NONE;
}

error_t apiStationGetIpv4Address(Device* device, char* ipv4) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ipv4 == nullptr) return ERROR_INVALID_ARGUMENT;

    mutex_lock(&ctx->mutex);
    bool connected = ctx->stationState == WIFI_STATION_STATE_CONNECTED;
    mutex_unlock(&ctx->mutex);

    strncpy(ipv4, connected ? MOCK_IPV4_ADDRESS : "0.0.0.0", 16);
    return ERROR_NONE;
}

error_t apiStationGetTargetSsid(Device* device, char* ssid) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ssid == nullptr) return ERROR_INVALID_ARGUMENT;
    mutex_lock(&ctx->mutex);
    strncpy(ssid, ctx->targetSsid, sizeof(ctx->targetSsid));
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

error_t apiStationConnect(Device* device, const char* ssid, const char* password, int32_t channel) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ssid == nullptr) return ERROR_INVALID_ARGUMENT;

    mutex_lock(&ctx->mutex);
    if (ctx->radioState != WIFI_RADIO_STATE_ON) {
        mutex_unlock(&ctx->mutex);
        return ERROR_INVALID_STATE;
    }
    strncpy(ctx->targetSsid, ssid, sizeof(ctx->targetSsid) - 1);
    ctx->targetSsid[sizeof(ctx->targetSsid) - 1] = '\0';
    ctx->stationState = WIFI_STATION_STATE_CONNECTION_PENDING;
    mutex_unlock(&ctx->mutex);

    WifiEvent pending_event = {};
    pending_event.type = WIFI_EVENT_TYPE_STATION_STATE_CHANGED;
    pending_event.station_state = WIFI_STATION_STATE_CONNECTION_PENDING;
    fireEvent(ctx, pending_event);

    // No real radio to negotiate with, so the mock always succeeds instantly.
    mutex_lock(&ctx->mutex);
    ctx->stationState = WIFI_STATION_STATE_CONNECTED;
    mutex_unlock(&ctx->mutex);

    WifiEvent connected_event = {};
    connected_event.type = WIFI_EVENT_TYPE_STATION_STATE_CHANGED;
    connected_event.station_state = WIFI_STATION_STATE_CONNECTED;
    fireEvent(ctx, connected_event);

    WifiEvent result_event = {};
    result_event.type = WIFI_EVENT_TYPE_STATION_CONNECTION_RESULT;
    result_event.connection_error = WIFI_STATION_CONNECTION_ERROR_NONE;
    fireEvent(ctx, result_event);

    return ERROR_NONE;
}

error_t apiStationDisconnect(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;

    mutex_lock(&ctx->mutex);
    bool was_connected = ctx->stationState != WIFI_STATION_STATE_DISCONNECTED;
    ctx->stationState = WIFI_STATION_STATE_DISCONNECTED;
    mutex_unlock(&ctx->mutex);

    if (was_connected) {
        WifiEvent event = {};
        event.type = WIFI_EVENT_TYPE_STATION_STATE_CHANGED;
        event.station_state = WIFI_STATION_STATE_DISCONNECTED;
        fireEvent(ctx, event);
    }

    return ERROR_NONE;
}

error_t apiStationGetRssi(Device* device, int32_t* rssi) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || rssi == nullptr) return ERROR_INVALID_ARGUMENT;

    mutex_lock(&ctx->mutex);
    bool connected = ctx->stationState == WIFI_STATION_STATE_CONNECTED;
    mutex_unlock(&ctx->mutex);

    if (!connected) return ERROR_INVALID_STATE;
    *rssi = MOCK_CONNECTED_RSSI;
    return ERROR_NONE;
}

error_t apiSetRadioOn(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;

    mutex_lock(&ctx->mutex);
    bool already_on = ctx->radioState == WIFI_RADIO_STATE_ON;
    ctx->radioState = WIFI_RADIO_STATE_ON;
    mutex_unlock(&ctx->mutex);
    if (already_on) return ERROR_NONE;

    WifiEvent radio_event = {};
    radio_event.type = WIFI_EVENT_TYPE_RADIO_STATE_CHANGED;
    radio_event.radio_state = WIFI_RADIO_STATE_ON;
    fireEvent(ctx, radio_event);

    LOG_I(TAG, "WiFi radio on (mock)");
    return ERROR_NONE;
}

error_t apiSetRadioOff(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;

    mutex_lock(&ctx->mutex);
    bool already_off = ctx->radioState == WIFI_RADIO_STATE_OFF;
    bool was_connected = ctx->stationState != WIFI_STATION_STATE_DISCONNECTED;
    ctx->radioState = WIFI_RADIO_STATE_OFF;
    ctx->stationState = WIFI_STATION_STATE_DISCONNECTED;
    ctx->scanning = false;
    mutex_unlock(&ctx->mutex);
    if (already_off) return ERROR_NONE;

    if (was_connected) {
        WifiEvent station_event = {};
        station_event.type = WIFI_EVENT_TYPE_STATION_STATE_CHANGED;
        station_event.station_state = WIFI_STATION_STATE_DISCONNECTED;
        fireEvent(ctx, station_event);
    }

    WifiEvent radio_event = {};
    radio_event.type = WIFI_EVENT_TYPE_RADIO_STATE_CHANGED;
    radio_event.radio_state = WIFI_RADIO_STATE_OFF;
    fireEvent(ctx, radio_event);

    LOG_I(TAG, "WiFi radio off (mock)");
    return ERROR_NONE;
}

error_t apiEventSubscribe(Device* device, WifiEventSubscription* sub, TaskEventGroup* event_group) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || sub == nullptr || event_group == nullptr) return ERROR_INVALID_ARGUMENT;

    uint32_t bit;
    error_t claim_result = task_event_group_claim_bit(event_group, &bit);
    if (claim_result != ERROR_NONE) {
        return claim_result;
    }

    mutex_lock(&ctx->subscriptionsMutex);

    // Avoid cyclic subscription list that would loop forever
    for (WifiEventSubscription* existing = ctx->subscriptions; existing != nullptr; existing = existing->internal.next) {
        if (existing == sub) {
            mutex_unlock(&ctx->subscriptionsMutex);
            task_event_group_release_bit(event_group, bit);
            return ERROR_INVALID_STATE;
        }
    }

    sub->internal.event_group = event_group;
    sub->bit = bit;
    sub->internal.next = ctx->subscriptions;
    ctx->subscriptions = sub;
    mutex_unlock(&ctx->subscriptionsMutex);
    return ERROR_NONE;
}

error_t apiEventUnsubscribe(Device* device, WifiEventSubscription* sub) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || sub == nullptr) return ERROR_INVALID_ARGUMENT;

    error_t result = ERROR_NOT_FOUND;
    mutex_lock(&ctx->subscriptionsMutex);
    for (WifiEventSubscription** link = &ctx->subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    mutex_unlock(&ctx->subscriptionsMutex);

    if (result == ERROR_NONE) {
        task_event_group_release_bit(sub->internal.event_group, sub->bit);
    }
    return result;
}

const WifiApi mock_wifi_api = {
    .set_radio_on = apiSetRadioOn,
    .set_radio_off = apiSetRadioOff,
    .get_radio_state = apiGetRadioState,
    .get_station_state = apiGetStationState,
    .get_access_point_state = apiGetAccessPointState,
    .is_scanning = apiIsScanning,
    .scan = apiScan,
    .get_scan_results = apiGetScanResults,
    .station_get_ipv4_address = apiStationGetIpv4Address,
    .station_get_target_ssid = apiStationGetTargetSsid,
    .station_connect = apiStationConnect,
    .station_disconnect = apiStationDisconnect,
    .station_get_rssi = apiStationGetRssi,
    .event_subscribe = apiEventSubscribe,
    .event_unsubscribe = apiEventUnsubscribe,
    .get_firmware_ops = nullptr,
    .access_point_start = apiAccessPointStart,
    .access_point_stop = apiAccessPointStop,
    .access_point_get_ipv4_address = apiAccessPointGetIpv4Address
};

// ---- Driver lifecycle ----
// startDevice()/stopDevice() only allocate/free this driver's bookkeeping (subscriber list
// included), so event subscribers can stay subscribed across radio on/off toggles - the radio
// itself is only touched by apiSetRadioOn()/apiSetRadioOff().

error_t startDevice(Device* device) {
    auto* ctx = new(std::nothrow) MockWifiCtx();
    if (ctx == nullptr) return ERROR_OUT_OF_MEMORY;

    ctx->device = device;
    mutex_construct(&ctx->mutex);
    mutex_construct(&ctx->subscriptionsMutex);

    device_set_driver_data(device, ctx);
    return ERROR_NONE;
}

error_t stopDevice(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_NONE;

    if (ctx->accessPointState == WIFI_ACCESS_POINT_STATE_STARTED) {
        apiAccessPointStop(device);
    }
    if (ctx->radioState == WIFI_RADIO_STATE_ON) {
        apiSetRadioOff(device);
    }

    // Release any subscribers that never unsubscribed: device_stop() doesn't wait for apps still
    // using this device, so a later wifi_event_unsubscribe() would find no ctx and skip releasing
    // the bit.
    mutex_lock(&ctx->subscriptionsMutex);
    for (WifiEventSubscription* sub = ctx->subscriptions; sub != nullptr;) {
        WifiEventSubscription* next = sub->internal.next;
        // Force-close, don't destruct: a concurrent wifi_event_poll() may hold/await this same
        // lock (see WifiEventSubscription::internal::closed).
        mutex_lock(&sub->internal.ring_mutex);
        sub->internal.closed = true;
        mutex_unlock(&sub->internal.ring_mutex);
        task_event_group_release_bit(sub->internal.event_group, sub->bit);
        sub = next;
    }
    ctx->subscriptions = nullptr;
    mutex_unlock(&ctx->subscriptionsMutex);

    device_set_driver_data(device, nullptr);
    mutex_destruct(&ctx->subscriptionsMutex);
    mutex_destruct(&ctx->mutex);
    delete ctx;

    return ERROR_NONE;
}

} // namespace

extern "C" {

Driver mock_wifi_driver = {
    .name = "mock_wifi",
    .compatible = (const char*[]) { "mock,wifi", nullptr },
    .start_device = startDevice,
    .stop_device = stopDevice,
    .probe = nullptr,
    .api = (const void*)&mock_wifi_api,
    .device_type = &WIFI_TYPE,
    .internal = nullptr
};

} // extern "C"
