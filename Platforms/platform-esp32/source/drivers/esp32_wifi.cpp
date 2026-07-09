#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_wifi.h>
#include <tactility/drivers/wifi.h>
#include <tactility/error_esp32.h>
#include <tactility/log.h>

#include <algorithm>
#include <cstring>
#include <new>

#define TAG "esp32_wifi"

namespace {

constexpr size_t WIFI_MAX_CALLBACKS = 4;
constexpr uint16_t WIFI_SCAN_RECORD_LIMIT = 32;

struct WifiCallbackEntry {
    WifiEventCallback fn = nullptr;
    void* ctx = nullptr;
};

struct Esp32WifiCtx {
    Device* device = nullptr;

    Mutex mutex{};
    WifiRadioState radioState = WIFI_RADIO_STATE_OFF;
    WifiStationState stationState = WIFI_STATION_STATE_DISCONNECTED;
    bool scanning = false;
    char targetSsid[33] = {};
    esp_netif_ip_info_t ipInfo{};
    wifi_ap_record_t scanResults[WIFI_SCAN_RECORD_LIMIT] = {};
    uint16_t scanResultCount = 0;

    esp_netif_t* netif = nullptr;
    esp_event_handler_instance_t wifiEventHandler = nullptr;
    esp_event_handler_instance_t ipEventHandler = nullptr;

    Mutex callbackMutex{};
    WifiCallbackEntry callbacks[WIFI_MAX_CALLBACKS] = {};
    size_t callbackCount = 0;
};

#define GET_CTX(device) (static_cast<Esp32WifiCtx*>(device_get_driver_data(device)))

WifiAuthenticationType to_wifi_authentication_type(wifi_auth_mode_t mode) {
    switch (mode) {
        case WIFI_AUTH_OPEN: return WIFI_AUTHENTICATION_TYPE_OPEN;
        case WIFI_AUTH_WEP: return WIFI_AUTHENTICATION_TYPE_WEP;
        case WIFI_AUTH_WPA_PSK: return WIFI_AUTHENTICATION_TYPE_WPA_PSK;
        case WIFI_AUTH_WPA2_PSK: return WIFI_AUTHENTICATION_TYPE_WPA2_PSK;
        case WIFI_AUTH_WPA_WPA2_PSK: return WIFI_AUTHENTICATION_TYPE_WPA_WPA2_PSK;
        case WIFI_AUTH_WPA2_ENTERPRISE: return WIFI_AUTHENTICATION_TYPE_WPA2_ENTERPRISE;
        case WIFI_AUTH_WPA3_PSK: return WIFI_AUTHENTICATION_TYPE_WPA3_PSK;
        case WIFI_AUTH_WPA2_WPA3_PSK: return WIFI_AUTHENTICATION_TYPE_WPA2_WPA3_PSK;
        case WIFI_AUTH_WAPI_PSK: return WIFI_AUTHENTICATION_TYPE_WAPI_PSK;
        case WIFI_AUTH_OWE: return WIFI_AUTHENTICATION_TYPE_OWE;
        case WIFI_AUTH_WPA3_ENT_192: return WIFI_AUTHENTICATION_TYPE_WPA3_ENT_192;
        case WIFI_AUTH_WPA3_EXT_PSK: return WIFI_AUTHENTICATION_TYPE_WPA3_EXT_PSK;
        case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE: return WIFI_AUTHENTICATION_TYPE_WPA3_EXT_PSK_MIXED_MODE;
        default: return WIFI_AUTHENTICATION_TYPE_OPEN;
    }
}

void fire_event(Esp32WifiCtx* ctx, WifiEvent event) {
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

// ---- ESP-IDF event handling (runs on the esp_event task) ----

void on_wifi_or_ip_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* ctx = static_cast<Esp32WifiCtx*>(arg);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        mutex_lock(&ctx->mutex);
        bool was_pending = ctx->stationState == WIFI_STATION_STATE_CONNECTION_PENDING;
        ctx->stationState = WIFI_STATION_STATE_DISCONNECTED;
        memset(&ctx->ipInfo, 0, sizeof(ctx->ipInfo));
        mutex_unlock(&ctx->mutex);

        WifiEvent state_event = {};
        state_event.type = WIFI_EVENT_TYPE_STATION_STATE_CHANGED;
        state_event.station_state = WIFI_STATION_STATE_DISCONNECTED;
        fire_event(ctx, state_event);

        if (was_pending) {
            WifiEvent result_event = {};
            result_event.type = WIFI_EVENT_TYPE_STATION_CONNECTION_RESULT;
            result_event.connection_error = WIFI_STATION_CONNECTION_ERROR_TARGET_NOT_FOUND;
            fire_event(ctx, result_event);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto* got_ip = static_cast<ip_event_got_ip_t*>(event_data);

        mutex_lock(&ctx->mutex);
        ctx->ipInfo = got_ip->ip_info;
        ctx->stationState = WIFI_STATION_STATE_CONNECTED;
        mutex_unlock(&ctx->mutex);

        WifiEvent state_event = {};
        state_event.type = WIFI_EVENT_TYPE_STATION_STATE_CHANGED;
        state_event.station_state = WIFI_STATION_STATE_CONNECTED;
        fire_event(ctx, state_event);

        WifiEvent result_event = {};
        result_event.type = WIFI_EVENT_TYPE_STATION_CONNECTION_RESULT;
        result_event.connection_error = WIFI_STATION_CONNECTION_ERROR_NONE;
        fire_event(ctx, result_event);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        mutex_lock(&ctx->mutex);
        ctx->scanning = false;
        uint16_t count = WIFI_SCAN_RECORD_LIMIT;
        esp_err_t err = esp_wifi_scan_get_ap_records(&count, ctx->scanResults);
        ctx->scanResultCount = (err == ESP_OK) ? count : 0;
        mutex_unlock(&ctx->mutex);

        WifiEvent event = {};
        event.type = WIFI_EVENT_TYPE_SCAN_FINISHED;
        fire_event(ctx, event);
    }
}

// ---- Work, always run inline on the caller (see note at the top) ----

error_t bring_up_wifi(Esp32WifiCtx* ctx) {
    ctx->netif = esp_netif_create_default_wifi_sta();
    if (ctx->netif == nullptr) {
        LOG_E(TAG, "Failed to create default STA netif");
        return ERROR_RESOURCE;
    }

    // Warning: this is the memory-intensive operation. It uses over 100kB of
    // RAM with default settings.
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        LOG_E(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        esp_netif_destroy(ctx->netif);
        ctx->netif = nullptr;
        return esp_err_to_error(err);
    }

    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_or_ip_event, ctx, &ctx->wifiEventHandler);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(err));
        esp_wifi_deinit();
        esp_netif_destroy(ctx->netif);
        ctx->netif = nullptr;
        return esp_err_to_error(err);
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_or_ip_event, ctx, &ctx->ipEventHandler);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(err));
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, ctx->wifiEventHandler);
        ctx->wifiEventHandler = nullptr;
        esp_wifi_deinit();
        esp_netif_destroy(ctx->netif);
        ctx->netif = nullptr;
        return esp_err_to_error(err);
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        LOG_E(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ctx->ipEventHandler);
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, ctx->wifiEventHandler);
        ctx->ipEventHandler = nullptr;
        ctx->wifiEventHandler = nullptr;
        esp_wifi_deinit();
        esp_netif_destroy(ctx->netif);
        ctx->netif = nullptr;
        return esp_err_to_error(err);
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        LOG_E(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ctx->ipEventHandler);
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, ctx->wifiEventHandler);
        ctx->ipEventHandler = nullptr;
        ctx->wifiEventHandler = nullptr;
        esp_wifi_set_mode(WIFI_MODE_NULL);
        esp_wifi_deinit();
        esp_netif_destroy(ctx->netif);
        ctx->netif = nullptr;
        return esp_err_to_error(err);
    }

    mutex_lock(&ctx->mutex);
    ctx->radioState = WIFI_RADIO_STATE_ON;
    mutex_unlock(&ctx->mutex);

    LOG_I(TAG, "WiFi radio on");
    return ERROR_NONE;
}

void bring_down_wifi(Esp32WifiCtx* ctx) {
    mutex_lock(&ctx->mutex);
    bool was_connected = ctx->stationState != WIFI_STATION_STATE_DISCONNECTED;
    bool was_scanning = ctx->scanning;
    ctx->stationState = WIFI_STATION_STATE_DISCONNECTED;
    ctx->scanning = false;
    mutex_unlock(&ctx->mutex);

    if (was_scanning) {
        esp_wifi_scan_stop();
    }
    if (was_connected) {
        esp_wifi_disconnect();
    }

    // Detach netif from the internal WiFi event handlers before stopping,
    // otherwise esp_netif_destroy() can race with esp_wifi_stop()'s own
    // netif teardown (see esp32_ble/WifiEsp.cpp for the same issue).
    if (ctx->netif != nullptr) {
        esp_wifi_clear_default_wifi_driver_and_handlers(ctx->netif);
    }

    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_NULL);

    if (ctx->wifiEventHandler != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, ctx->wifiEventHandler);
        ctx->wifiEventHandler = nullptr;
    }
    if (ctx->ipEventHandler != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ctx->ipEventHandler);
        ctx->ipEventHandler = nullptr;
    }

    esp_wifi_deinit();

    if (ctx->netif != nullptr) {
        esp_netif_destroy(ctx->netif);
        ctx->netif = nullptr;
    }

    mutex_lock(&ctx->mutex);
    ctx->radioState = WIFI_RADIO_STATE_OFF;
    mutex_unlock(&ctx->mutex);

    LOG_I(TAG, "WiFi radio off");
}

// ---- WifiApi ----

error_t api_get_radio_state(Device* device, WifiRadioState* state) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || state == nullptr) return ERROR_INVALID_ARGUMENT;
    mutex_lock(&ctx->mutex);
    *state = ctx->radioState;
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

error_t api_get_station_state(Device* device, WifiStationState* state) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || state == nullptr) return ERROR_INVALID_ARGUMENT;
    mutex_lock(&ctx->mutex);
    *state = ctx->stationState;
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

error_t api_get_access_point_state(Device* device, WifiAccessPointState* state) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || state == nullptr) return ERROR_INVALID_ARGUMENT;
    // Access point mode isn't implemented by this driver.
    *state = WIFI_ACCESS_POINT_STATE_STOPPED;
    return ERROR_NONE;
}

bool api_is_scanning(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return false;
    mutex_lock(&ctx->mutex);
    bool scanning = ctx->scanning;
    mutex_unlock(&ctx->mutex);
    return scanning;
}

error_t api_scan(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;

    mutex_lock(&ctx->mutex);
    if (ctx->radioState != WIFI_RADIO_STATE_ON || ctx->scanning) {
        mutex_unlock(&ctx->mutex);
        return ERROR_INVALID_STATE;
    }
    mutex_unlock(&ctx->mutex);

    esp_err_t err = esp_wifi_scan_start(nullptr, false);
    if (err != ESP_OK) {
        return esp_err_to_error(err);
    }

    mutex_lock(&ctx->mutex);
    ctx->scanning = true;
    mutex_unlock(&ctx->mutex);

    WifiEvent event = {};
    event.type = WIFI_EVENT_TYPE_SCAN_STARTED;
    fire_event(ctx, event);
    return ERROR_NONE;
}

error_t api_get_scan_results(Device* device, WifiApRecord* results, size_t* num_results) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || results == nullptr || num_results == nullptr) return ERROR_INVALID_ARGUMENT;

    mutex_lock(&ctx->mutex);
    size_t count = std::min<size_t>(*num_results, ctx->scanResultCount);
    for (size_t i = 0; i < count; i++) {
        const wifi_ap_record_t& src = ctx->scanResults[i];
        WifiApRecord& dst = results[i];
        memset(dst.ssid, 0, sizeof(dst.ssid));
        memcpy(dst.ssid, src.ssid, std::min(sizeof(dst.ssid) - 1, sizeof(src.ssid)));
        dst.rssi = src.rssi;
        dst.channel = src.primary;
        dst.authentication_type = to_wifi_authentication_type(src.authmode);
    }
    *num_results = count;
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

error_t api_station_get_ipv4_address(Device* device, char* ipv4) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ipv4 == nullptr) return ERROR_INVALID_ARGUMENT;
    mutex_lock(&ctx->mutex);
    esp_ip4addr_ntoa(&ctx->ipInfo.ip, ipv4, 16);
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

error_t api_station_get_target_ssid(Device* device, char* ssid) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ssid == nullptr) return ERROR_INVALID_ARGUMENT;
    mutex_lock(&ctx->mutex);
    constexpr size_t ssid_buffer_size = sizeof(ctx->targetSsid);
    strncpy(ssid, ctx->targetSsid, ssid_buffer_size);
    mutex_unlock(&ctx->mutex);
    return ERROR_NONE;
}

error_t api_station_connect(Device* device, const char* ssid, const char* password, int32_t channel) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ssid == nullptr) return ERROR_INVALID_ARGUMENT;

    mutex_lock(&ctx->mutex);
    bool radio_on = ctx->radioState == WIFI_RADIO_STATE_ON;
    bool was_connected = ctx->stationState != WIFI_STATION_STATE_DISCONNECTED;
    mutex_unlock(&ctx->mutex);

    if (!radio_on) {
        return ERROR_INVALID_STATE;
    }

    if (was_connected) {
        esp_wifi_disconnect();
    }

    wifi_config_t config {};
    config.sta.channel = static_cast<uint8_t>(channel);
    config.sta.scan_method = WIFI_FAST_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.rssi = -127;
    config.sta.pmf_cfg.capable = true;
    strncpy(reinterpret_cast<char*>(config.sta.ssid), ssid, sizeof(config.sta.ssid) - 1);
    if (password != nullptr && password[0] != '\0') {
        strncpy(reinterpret_cast<char*>(config.sta.password), password, sizeof(config.sta.password) - 1);
        config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) {
        return esp_err_to_error(err);
    }

    mutex_lock(&ctx->mutex);
    strncpy(ctx->targetSsid, ssid, sizeof(ctx->targetSsid) - 1);
    ctx->targetSsid[sizeof(ctx->targetSsid) - 1] = '\0';
    ctx->stationState = WIFI_STATION_STATE_CONNECTION_PENDING;
    mutex_unlock(&ctx->mutex);

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        mutex_lock(&ctx->mutex);
        ctx->stationState = WIFI_STATION_STATE_DISCONNECTED;
        mutex_unlock(&ctx->mutex);
        return esp_err_to_error(err);
    }

    WifiEvent event = {};
    event.type = WIFI_EVENT_TYPE_STATION_STATE_CHANGED;
    event.station_state = WIFI_STATION_STATE_CONNECTION_PENDING;
    fire_event(ctx, event);
    return ERROR_NONE;
}

error_t api_station_disconnect(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;

    mutex_lock(&ctx->mutex);
    bool was_connected = ctx->stationState != WIFI_STATION_STATE_DISCONNECTED;
    mutex_unlock(&ctx->mutex);

    if (!was_connected) {
        return ERROR_NONE;
    }

    // The DISCONNECTED state change is published by the WIFI_EVENT handler
    // once ESP-IDF confirms the disconnect, so we don't fire it here.
    esp_err_t err = esp_wifi_disconnect();
    return err == ESP_OK ? ERROR_NONE : esp_err_to_error(err);
}

error_t api_station_get_rssi(Device* device, int32_t* rssi) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || rssi == nullptr) return ERROR_INVALID_ARGUMENT;

    int native_rssi = 0;
    esp_err_t err = esp_wifi_sta_get_rssi(&native_rssi);
    if (err != ESP_OK) {
        return esp_err_to_error(err);
    }
    *rssi = native_rssi;
    return ERROR_NONE;
}

error_t api_add_event_callback(Device* device, void* callback_context, WifiEventCallback callback) {
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

error_t api_remove_event_callback(Device* device, WifiEventCallback callback) {
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

const WifiApi esp32_wifi_api = {
    .get_radio_state = api_get_radio_state,
    .get_station_state = api_get_station_state,
    .get_access_point_state = api_get_access_point_state,
    .is_scanning = api_is_scanning,
    .scan = api_scan,
    .get_scan_results = api_get_scan_results,
    .station_get_ipv4_address = api_station_get_ipv4_address,
    .station_get_target_ssid = api_station_get_target_ssid,
    .station_connect = api_station_connect,
    .station_disconnect = api_station_disconnect,
    .station_get_rssi = api_station_get_rssi,
    .add_event_callback = api_add_event_callback,
    .remove_event_callback = api_remove_event_callback
};

// ---- Driver lifecycle ----
// The ESP-IDF WiFi stack isn't touched until the device is actually started:
// registering this driver (module start()) only makes it available for
// binding, it doesn't spin up any resources.

error_t start_device(Device* device) {
    auto* ctx = new(std::nothrow) Esp32WifiCtx();
    if (ctx == nullptr) return ERROR_OUT_OF_MEMORY;

    ctx->device = device;
    mutex_construct(&ctx->mutex);
    mutex_construct(&ctx->callbackMutex);
    device_set_driver_data(device, ctx);

    error_t result = bring_up_wifi(ctx);
    if (result != ERROR_NONE) {
        device_set_driver_data(device, nullptr);
        mutex_destruct(&ctx->callbackMutex);
        mutex_destruct(&ctx->mutex);
        delete ctx;
        return result;
    }

    return ERROR_NONE;
}

error_t stop_device(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_NONE;

    bring_down_wifi(ctx);

    device_set_driver_data(device, nullptr);
    mutex_destruct(&ctx->callbackMutex);
    mutex_destruct(&ctx->mutex);
    delete ctx;

    return ERROR_NONE;
}

} // namespace

extern "C" {

extern Module platform_esp32_module;

Driver esp32_wifi_driver = {
    .name = "esp32_wifi",
    .compatible = (const char*[]) { "espressif,esp32-wifi", nullptr },
    .start_device = start_device,
    .stop_device = stop_device,
    .api = (const void*)&esp32_wifi_api,
    .device_type = &WIFI_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};

} // extern "C"

#endif // CONFIG_SOC_WIFI_SUPPORTED or CONFIG_SLAVE_SOC_WIFI_SUPPORTED
