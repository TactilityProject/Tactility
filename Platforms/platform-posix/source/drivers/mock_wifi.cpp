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

constexpr size_t WIFI_MAX_CALLBACKS = 4;

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

struct WifiCallbackEntry {
    WifiEventCallback fn = nullptr;
    void* ctx = nullptr;
};

struct PosixWifiCtx {
    Device* device = nullptr;

    Mutex mutex {};
    WifiRadioState radioState = WIFI_RADIO_STATE_OFF;
    WifiStationState stationState = WIFI_STATION_STATE_DISCONNECTED;
    bool scanning = false;
    char targetSsid[33] = {};

    Mutex callbackMutex {};
    WifiCallbackEntry callbacks[WIFI_MAX_CALLBACKS] = {};
    size_t callbackCount = 0;
};

#define GET_CTX(device) (static_cast<PosixWifiCtx*>(device_get_driver_data(device)))

void fireEvent(PosixWifiCtx* ctx, WifiEvent event) {
    WifiCallbackEntry local[WIFI_MAX_CALLBACKS];
    size_t count;
    mutex_lock(&ctx->callbackMutex);
    count = ctx->callbackCount;
    memcpy(local, ctx->callbacks, count * sizeof(WifiCallbackEntry));
    mutex_unlock(&ctx->callbackMutex);

    for (size_t i = 0; i < count; i++) {
        local[i].fn(ctx->device, local[i].ctx, event);
    }
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
    // Access point mode isn't implemented by this mock.
    *state = WIFI_ACCESS_POINT_STATE_STOPPED;
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

error_t apiAddEventCallback(Device* device, void* callback_context, WifiEventCallback callback) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || callback == nullptr) return ERROR_INVALID_ARGUMENT;

    mutex_lock(&ctx->callbackMutex);
    if (ctx->callbackCount >= WIFI_MAX_CALLBACKS) {
        mutex_unlock(&ctx->callbackMutex);
        return ERROR_OUT_OF_MEMORY;
    }
    ctx->callbacks[ctx->callbackCount] = { .fn = callback, .ctx = callback_context };
    ctx->callbackCount++;
    mutex_unlock(&ctx->callbackMutex);
    return ERROR_NONE;
}

error_t apiRemoveEventCallback(Device* device, WifiEventCallback callback) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || callback == nullptr) return ERROR_INVALID_ARGUMENT;

    mutex_lock(&ctx->callbackMutex);
    for (size_t i = 0; i < ctx->callbackCount; i++) {
        if (ctx->callbacks[i].fn == callback) {
            for (size_t j = i; j + 1 < ctx->callbackCount; j++) {
                ctx->callbacks[j] = ctx->callbacks[j + 1];
            }
            ctx->callbackCount--;
            mutex_unlock(&ctx->callbackMutex);
            return ERROR_NONE;
        }
    }
    mutex_unlock(&ctx->callbackMutex);
    return ERROR_NOT_FOUND;
}

const WifiApi posix_wifi_api = {
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
    .add_event_callback = apiAddEventCallback,
    .remove_event_callback = apiRemoveEventCallback
};

// ---- Driver lifecycle ----
// Unlike the real esp32 driver, there's no hardware to bring up: the radio
// simply reports itself as ON as soon as the device is started.

error_t startDevice(Device* device) {
    auto* ctx = new(std::nothrow) PosixWifiCtx();
    if (ctx == nullptr) return ERROR_OUT_OF_MEMORY;

    ctx->device = device;
    mutex_construct(&ctx->mutex);
    mutex_construct(&ctx->callbackMutex);
    ctx->radioState = WIFI_RADIO_STATE_ON;

    device_set_driver_data(device, ctx);

    LOG_I(TAG, "WiFi radio on (mock)");
    return ERROR_NONE;
}

error_t stopDevice(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_NONE;

    device_set_driver_data(device, nullptr);
    mutex_destruct(&ctx->callbackMutex);
    mutex_destruct(&ctx->mutex);
    delete ctx;

    LOG_I(TAG, "WiFi radio off (mock)");
    return ERROR_NONE;
}

} // namespace

extern "C" {

extern Module platform_posix_module;

Driver posix_wifi_driver = {
    .name = "mock_wifi",
    .compatible = (const char*[]) { "posix,mock-wifi", nullptr },
    .start_device = startDevice,
    .stop_device = stopDevice,
    .api = (const void*)&posix_wifi_api,
    .device_type = &WIFI_TYPE,
    .owner = &platform_posix_module,
    .internal = nullptr
};

} // extern "C"
