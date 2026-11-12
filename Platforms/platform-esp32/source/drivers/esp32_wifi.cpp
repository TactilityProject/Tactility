#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLED)

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_wifi.h>
#include <tactility/drivers/esp32_wifi_radio.h>
#include <tactility/drivers/wifi.h>
#include <tactility/error_esp32.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <tactility/Allocator.hpp>

#if defined(CONFIG_ESP_HOSTED_ENABLED)
#include <tactility/drivers/esp32_esp_hosted_ota.h>
#endif

#include "tactility/system_event.h"


#include <algorithm>
#include <cstring>
#include <mutex>
#include <new>

#define TAG "esp32_wifi"

namespace {

constexpr uint16_t WIFI_SCAN_RECORD_LIMIT = 32;

struct Esp32WifiCtx {
    Device* device = nullptr;

    Mutex mutex{};
    WifiRadioState radioState = WIFI_RADIO_STATE_OFF;
    WifiStationState stationState = WIFI_STATION_STATE_DISCONNECTED;
    WifiAccessPointState accessPointState = WIFI_ACCESS_POINT_STATE_STOPPED;
    // Exists while the access point runs
    esp_netif_t* accessPointNetif = nullptr;
    bool scanning = false;
    char targetSsid[33] = {};
    esp_netif_ip_info_t ipInfo{};
    wifi_ap_record_t scanResults[WIFI_SCAN_RECORD_LIMIT] = {};
    uint16_t scanResultCount = 0;

    esp_event_handler_instance_t wifiEventHandler = nullptr;
    esp_event_handler_instance_t ipEventHandler = nullptr;

    // Dedup for WIFI_EVENT/IP_EVENT notifications: on the esp_hosted/Wi-Fi Remote transport
    // (e.g. Tab5's P4 host + C6 co-processor), the RPC layer has been observed delivering the
    // exact same event twice in a row (same base, same event_id, same millisecond - not two
    // genuinely separate occurrences). Native WiFi doesn't exhibit this, but the handler is
    // shared, so the guard applies unconditionally; it's a no-op for well-separated real events.
    esp_event_base_t lastEventBase = nullptr;
    int32_t lastEventId = -1;
    TickType_t lastEventTick = 0;

    Mutex subscriptionsMutex{};
    WifiEventSubscription* subscriptions = nullptr;
};

#define GET_CTX(device) (static_cast<Esp32WifiCtx*>(device_get_driver_data(device)))

// Shared radio ownership state for esp32_wifi_radio_acquire()/release() (see esp32_wifi_radio.h).
// Station mode and access point mode (this driver) and ESP-NOW acquire/release their own
// mode bit rather than calling esp_wifi_init()/deinit() directly, so neither tears down the radio
// out from under the other.
Mutex g_radioMutex{};
int g_radioOwnerCount = 0;
wifi_mode_t g_radioMode = WIFI_MODE_NULL;
// Owners per mode bit, as several owners can share one: a bit leaves the mode when its last owner released
int g_staOwnerCount = 0;
int g_apOwnerCount = 0;
// The station netif exists for as long as the station bit is active, and it's created before the radio starts,
// so the netif always handles the station's start event. A netif that is created after the radio started doesn't get an IP address.
esp_netif_t* g_staNetif = nullptr;

// Netifs that were started and not stopped or destroyed since
std::mutex g_startedNetifMutex;
const void* g_startedNetifs[4] = {};

bool mark_netif_started(const void* netif) {
    std::lock_guard<std::mutex> lock(g_startedNetifMutex);
    const void** free_slot = nullptr;
    for (auto& slot : g_startedNetifs) {
        if (slot == netif) {
            return false;
        }
        if (slot == nullptr && free_slot == nullptr) {
            free_slot = &slot;
        }
    }
    if (free_slot != nullptr) {
        *free_slot = netif;
    }
    return true;
}

void mark_netif_stopped(const void* netif) {
    std::lock_guard<std::mutex> lock(g_startedNetifMutex);
    for (auto& slot : g_startedNetifs) {
        if (slot == netif) {
            slot = nullptr;
        }
    }
}

// Must be called with the radio mutex held
void destroy_station_netif_if(bool destroy) {
    if (destroy && g_staNetif != nullptr) {
        esp_wifi_clear_default_wifi_driver_and_handlers(g_staNetif);
        esp_netif_destroy(g_staNetif);
        g_staNetif = nullptr;
    }
}

int& modeOwnerCount(wifi_mode_t mode_bit) {
    return mode_bit == WIFI_MODE_AP ? g_apOwnerCount : g_staOwnerCount;
}

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
        default: return WIFI_AUTHENTICATION_TYPE_OPEN;
    }
}

void fire_event(Esp32WifiCtx* ctx, WifiEvent event) {
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

// ---- ESP-IDF event handling (runs on the esp_event task) ----

void on_wifi_or_ip_event(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* ctx = static_cast<Esp32WifiCtx*>(arg);

    // See Esp32WifiCtx::lastEventBase/lastEventId/lastEventTick - collapse an immediate duplicate
    // delivery of the same event (observed on the esp_hosted/Wi-Fi Remote transport) into one.
    constexpr uint32_t DEDUP_WINDOW_MS = 50; // well under any real re-occurrence of the same event
    TickType_t now = get_ticks();
    bool is_duplicate = event_base == ctx->lastEventBase && event_id == ctx->lastEventId &&
        (now - ctx->lastEventTick) <= millis_to_ticks(DEDUP_WINDOW_MS);
    ctx->lastEventBase = event_base;
    ctx->lastEventId = event_id;
    ctx->lastEventTick = now;
    if (is_duplicate) {
        LOG_D(TAG, "Ignoring duplicate WiFi event %d", (int)event_id);
        return;
    }

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
            NetworkDisconnectedEvent disconnected_event = {
                .device = ctx->device
            };
            system_event_emit(KERNEL_EVENT_NETWORK_DISCONNECTED, &disconnected_event, sizeof(disconnected_event));
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

        NetworkConnectedEvent connected_event = {
            .device = ctx->device,
            .ipv4_addr = ctx->ipInfo.ip.addr,
            .gateway = ctx->ipInfo.gw.addr,
        };
        system_event_emit(KERNEL_EVENT_NETWORK_CONNECTED, &connected_event, sizeof(connected_event));
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
    // Warning: this is the memory-intensive operation. It uses over 100kB of
    // RAM with default settings. It also creates the station netif.
    if (!esp32_wifi_radio_acquire(WIFI_MODE_STA)) {
        LOG_E(TAG, "Failed to acquire WiFi radio for station mode");
        return ERROR_RESOURCE;
    }

    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    esp_err_t err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_or_ip_event, ctx, &ctx->wifiEventHandler);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(err));
        esp32_wifi_radio_release(WIFI_MODE_STA);
        return esp_err_to_error(err);
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_or_ip_event, ctx, &ctx->ipEventHandler);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(err));
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, ctx->wifiEventHandler);
        ctx->wifiEventHandler = nullptr;
        esp32_wifi_radio_release(WIFI_MODE_STA);
        return esp_err_to_error(err);
    }

    mutex_lock(&ctx->mutex);
    ctx->radioState = WIFI_RADIO_STATE_ON;
    mutex_unlock(&ctx->mutex);

    WifiEvent radio_event = {};
    radio_event.type = WIFI_EVENT_TYPE_RADIO_STATE_CHANGED;
    radio_event.radio_state = WIFI_RADIO_STATE_ON;
    fire_event(ctx, radio_event);

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

    if (ctx->wifiEventHandler != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, ctx->wifiEventHandler);
        ctx->wifiEventHandler = nullptr;
    }
    if (ctx->ipEventHandler != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ctx->ipEventHandler);
        ctx->ipEventHandler = nullptr;
    }

    // The station netif is destroyed with the last station owner of the radio
    esp32_wifi_radio_release(WIFI_MODE_STA);

    mutex_lock(&ctx->mutex);
    ctx->radioState = WIFI_RADIO_STATE_OFF;
    mutex_unlock(&ctx->mutex);

    WifiEvent radio_event = {};
    radio_event.type = WIFI_EVENT_TYPE_RADIO_STATE_CHANGED;
    radio_event.radio_state = WIFI_RADIO_STATE_OFF;
    fire_event(ctx, radio_event);

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
    mutex_lock(&ctx->mutex);
    *state = ctx->accessPointState;
    mutex_unlock(&ctx->mutex);
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

constexpr auto* ACCESS_POINT_ADDRESS = "192.168.4.1";
constexpr uint8_t ACCESS_POINT_DEFAULT_CHANNEL = 1;
constexpr uint8_t ACCESS_POINT_MAX_CHANNEL = 13;
constexpr uint8_t ACCESS_POINT_DEFAULT_MAX_CONNECTIONS = 4;

// Detaches the netif from the internal WiFi event handlers before it is destroyed,
// otherwise the Wi-Fi driver keeps a pointer to the freed netif.
void destroy_access_point_netif(Esp32WifiCtx* ctx) {
    if (ctx->accessPointNetif != nullptr) {
        esp_wifi_clear_default_wifi_driver_and_handlers(ctx->accessPointNetif);
        esp_netif_destroy(ctx->accessPointNetif);
        ctx->accessPointNetif = nullptr;
    }
}

// The netif is detached before the radio is released, otherwise esp_netif_destroy() can race with esp_wifi_stop()'s own netif teardown.
void release_access_point(Esp32WifiCtx* ctx) {
    if (ctx->accessPointNetif != nullptr) {
        esp_wifi_clear_default_wifi_driver_and_handlers(ctx->accessPointNetif);
    }
    esp32_wifi_radio_release(WIFI_MODE_AP);
    if (ctx->accessPointNetif != nullptr) {
        esp_netif_destroy(ctx->accessPointNetif);
        ctx->accessPointNetif = nullptr;
    }
}

void fire_access_point_event(Esp32WifiCtx* ctx, WifiAccessPointState state) {
    WifiEvent event = {};
    event.type = WIFI_EVENT_TYPE_ACCESS_POINT_STATE_CHANGED;
    event.access_point_state = state;
    fire_event(ctx, event);
}

error_t api_access_point_start(Device* device, const WifiAccessPointConfig* config) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;
    if (config == nullptr) return ERROR_INVALID_ARGUMENT;

    const size_t ssid_length = strnlen(config->ssid, sizeof(config->ssid));
    const size_t password_length = strnlen(config->password, sizeof(config->password));
    const bool ssid_valid = ssid_length > 0 && ssid_length < sizeof(config->ssid);
    const bool password_valid = password_length == 0 || (password_length >= 8 && password_length < sizeof(config->password));
    if (!ssid_valid || !password_valid || config->channel > ACCESS_POINT_MAX_CHANNEL || config->max_connections > ESP_WIFI_MAX_CONN_NUM) {
        return ERROR_INVALID_ARGUMENT;
    }

    mutex_lock(&ctx->mutex);
    const bool started = ctx->accessPointState == WIFI_ACCESS_POINT_STATE_STARTED;
    mutex_unlock(&ctx->mutex);
    if (started) return ERROR_INVALID_STATE;

    // The netif and its address exist before the radio starts, so that the netif handles the start event of the access point
    ctx->accessPointNetif = esp_netif_create_default_wifi_ap();
    if (ctx->accessPointNetif == nullptr) {
        LOG_E(TAG, "esp_netif_create_default_wifi_ap() failed");
        return ERROR_RESOURCE;
    }

    // A static address, so clients always find the access point at the same place
    esp_netif_ip_info_t ip_info {};
    ip_info.ip.addr = ESP_IP4TOADDR(192, 168, 4, 1);
    ip_info.gw.addr = ESP_IP4TOADDR(192, 168, 4, 1);
    ip_info.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);

    const esp_err_t stop_dhcp_result = esp_netif_dhcps_stop(ctx->accessPointNetif);
    const esp_err_t set_ip_result = esp_netif_set_ip_info(ctx->accessPointNetif, &ip_info);
    const esp_err_t start_dhcp_result = esp_netif_dhcps_start(ctx->accessPointNetif);
    if ((stop_dhcp_result != ESP_OK && stop_dhcp_result != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) ||
        set_ip_result != ESP_OK ||
        (start_dhcp_result != ESP_OK && start_dhcp_result != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)) {
        LOG_E(TAG, "Failed to set up the access point network");
        destroy_access_point_netif(ctx);
        return ERROR_RESOURCE;
    }

    // Acquires the shared radio, which merges into WIFI_MODE_APSTA when the station owns the radio already
    if (!esp32_wifi_radio_acquire(WIFI_MODE_AP)) {
        LOG_E(TAG, "Failed to acquire the radio for the access point");
        destroy_access_point_netif(ctx);
        return ERROR_RESOURCE;
    }

    wifi_config_t wifi_config {};
    // The SSID field has no terminator for a name of 32 bytes: ssid_len gives the length
    memcpy(wifi_config.ap.ssid, config->ssid, ssid_length);
    wifi_config.ap.ssid_len = static_cast<uint8_t>(ssid_length);
    if (password_length > 0) {
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        strncpy(reinterpret_cast<char*>(wifi_config.ap.password), config->password, sizeof(wifi_config.ap.password) - 1);
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    wifi_config.ap.max_connection = config->max_connections != 0 ? config->max_connections : ACCESS_POINT_DEFAULT_MAX_CONNECTIONS;
    wifi_config.ap.channel = config->channel != 0 ? config->channel : ACCESS_POINT_DEFAULT_CHANNEL;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        LOG_E(TAG, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(err));
        release_access_point(ctx);
        return ERROR_RESOURCE;
    }

    mutex_lock(&ctx->mutex);
    ctx->accessPointState = WIFI_ACCESS_POINT_STATE_STARTED;
    mutex_unlock(&ctx->mutex);
    fire_access_point_event(ctx, WIFI_ACCESS_POINT_STATE_STARTED);

    LOG_I(TAG, "Access point started: SSID '%s', channel %u, %s", config->ssid, static_cast<unsigned>(wifi_config.ap.channel), ACCESS_POINT_ADDRESS);
    return ERROR_NONE;
}

error_t api_access_point_stop(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;

    mutex_lock(&ctx->mutex);
    const bool started = ctx->accessPointState == WIFI_ACCESS_POINT_STATE_STARTED;
    ctx->accessPointState = WIFI_ACCESS_POINT_STATE_STOPPED;
    mutex_unlock(&ctx->mutex);
    if (!started) return ERROR_NONE;

    release_access_point(ctx);
    fire_access_point_event(ctx, WIFI_ACCESS_POINT_STATE_STOPPED);
    LOG_I(TAG, "Access point stopped");
    return ERROR_NONE;
}

error_t api_access_point_get_ipv4_address(Device* device, char* ipv4) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr || ipv4 == nullptr) return ERROR_INVALID_ARGUMENT;

    mutex_lock(&ctx->mutex);
    const bool started = ctx->accessPointState == WIFI_ACCESS_POINT_STATE_STARTED;
    mutex_unlock(&ctx->mutex);
    if (!started) return ERROR_INVALID_STATE;

    strcpy(ipv4, ACCESS_POINT_ADDRESS);
    return ERROR_NONE;
}

error_t api_set_radio_on(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;

    mutex_lock(&ctx->mutex);
    bool already_on = ctx->radioState == WIFI_RADIO_STATE_ON;
    mutex_unlock(&ctx->mutex);
    if (already_on) return ERROR_NONE;

    return bring_up_wifi(ctx);
}

error_t api_set_radio_off(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_INVALID_STATE;

    mutex_lock(&ctx->mutex);
    bool already_off = ctx->radioState == WIFI_RADIO_STATE_OFF;
    mutex_unlock(&ctx->mutex);
    if (already_off) return ERROR_NONE;

    bring_down_wifi(ctx);
    return ERROR_NONE;
}

error_t api_event_subscribe(Device* device, WifiEventSubscription* sub, TaskEventGroup* event_group) {
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

error_t api_event_unsubscribe(Device* device, WifiEventSubscription* sub) {
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

error_t api_get_firmware_ops(Device* /*device*/, const FirmwareOps** ops, void** ctx) {
    // ops/ctx are caller-supplied output pointers, reachable from external (ELF) apps via
    // wifi_get_firmware_ops() - validate at this API boundary rather than trusting the caller.
    if (ops == nullptr || ctx == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }
#if defined(CONFIG_ESP_HOSTED_ENABLED)
    // Only meaningful on a hosted board (P4+C6/C5 etc.) - this wifi device is backed by a real
    // co-processor with its own updatable firmware there. On a native (non-hosted) chip, this
    // device's "radio" is the chip's own built-in WiFi, nothing to update via this interface.
    *ops = esp32_esp_hosted_ota_get_ops();
    *ctx = nullptr; // esp32_esp_hosted_ota's FirmwareOps functions are all singleton/global, no per-call ctx needed
    return ERROR_NONE;
#else
    return ERROR_NOT_SUPPORTED;
#endif
}

const WifiApi esp32_wifi_api = {
    .set_radio_on = api_set_radio_on,
    .set_radio_off = api_set_radio_off,
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
    .event_subscribe = api_event_subscribe,
    .event_unsubscribe = api_event_unsubscribe,
    .get_firmware_ops = api_get_firmware_ops,
    .access_point_start = api_access_point_start,
    .access_point_stop = api_access_point_stop,
    .access_point_get_ipv4_address = api_access_point_get_ipv4_address
};

error_t start_device(Device* device) {
    // Prefers PSRAM/SPIRAM, falling back to internal RAM when unavailable - aborts on true OOM
    // (see OptExternalAllocator's own doc), so no null check here.
    tt::OptExternalAllocator<Esp32WifiCtx> allocator;
    auto* ctx = allocator.allocate(1);
    new (ctx) Esp32WifiCtx();

    ctx->device = device;
    mutex_construct(&ctx->mutex);
    mutex_construct(&ctx->subscriptionsMutex);
    mutex_construct(&g_radioMutex);
    device_set_driver_data(device, ctx);

    return ERROR_NONE;
}

error_t stop_device(Device* device) {
    auto* ctx = GET_CTX(device);
    if (ctx == nullptr) return ERROR_NONE;

    api_access_point_stop(device);

    if (ctx->radioState == WIFI_RADIO_STATE_ON) {
        bring_down_wifi(ctx);
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
    mutex_destruct(&g_radioMutex);
    ctx->~Esp32WifiCtx();
    tt::OptExternalAllocator<Esp32WifiCtx>().deallocate(ctx, 1);

    return ERROR_NONE;
}

} // namespace

extern "C" {

bool esp32_wifi_radio_acquire(wifi_mode_t mode_bit) {
    bool was_started;
    return esp32_wifi_radio_acquire_ex(mode_bit, &was_started);
}

bool esp32_wifi_radio_acquire_ex(wifi_mode_t mode_bit, bool* was_started) {
    mutex_lock(&g_radioMutex);

    bool first_owner = g_radioOwnerCount == 0;
    *was_started = !first_owner;

    // Before the radio starts, so that the netif handles the station's start event
    bool created_netif = false;
    if (mode_bit == WIFI_MODE_STA && g_staNetif == nullptr) {
        g_staNetif = esp_netif_create_default_wifi_sta();
        if (g_staNetif == nullptr) {
            LOG_E(TAG, "esp32_wifi_radio_acquire: failed to create the station netif");
            mutex_unlock(&g_radioMutex);
            return false;
        }
        created_netif = true;
    }

    if (first_owner) {
        wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&init_config);
        if (err != ESP_OK) {
            LOG_E(TAG, "esp32_wifi_radio_acquire: esp_wifi_init failed: %s", esp_err_to_name(err));
            destroy_station_netif_if(created_netif);
            mutex_unlock(&g_radioMutex);
            return false;
        }
    }

    auto new_mode = static_cast<wifi_mode_t>(g_radioMode | mode_bit);
    // Setting the same mode again can make the station restart, which sends another WIFI_EVENT_STA_START
    esp_err_t err = (new_mode == g_radioMode && !first_owner) ? ESP_OK : esp_wifi_set_mode(new_mode);
    if (err != ESP_OK) {
        LOG_E(TAG, "esp32_wifi_radio_acquire: esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        if (first_owner) {
            esp_wifi_deinit();
        }
        destroy_station_netif_if(created_netif);
        mutex_unlock(&g_radioMutex);
        return false;
    }

    if (first_owner) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            LOG_E(TAG, "esp32_wifi_radio_acquire: esp_wifi_start failed: %s", esp_err_to_name(err));
            esp_wifi_set_mode(WIFI_MODE_NULL);
            esp_wifi_deinit();
            destroy_station_netif_if(created_netif);
            mutex_unlock(&g_radioMutex);
            return false;
        }
    }

    g_radioMode = new_mode;
    g_radioOwnerCount++;
    modeOwnerCount(mode_bit)++;
    mutex_unlock(&g_radioMutex);
    return true;
}

bool esp32_wifi_radio_is_started(void) {
    mutex_lock(&g_radioMutex);
    const bool started = g_radioOwnerCount > 0;
    mutex_unlock(&g_radioMutex);
    return started;
}

void esp32_wifi_radio_release(wifi_mode_t mode_bit) {
    mutex_lock(&g_radioMutex);

    if (g_radioOwnerCount <= 0) {
        mutex_unlock(&g_radioMutex);
        return; // Guards against a mismatched acquire/release pair.
    }

    const wifi_mode_t previous_mode = g_radioMode;
    if (--modeOwnerCount(mode_bit) <= 0) {
        modeOwnerCount(mode_bit) = 0;
        g_radioMode = static_cast<wifi_mode_t>(g_radioMode & ~mode_bit);
    }
    g_radioOwnerCount--;

    // Detach the station netif from the internal WiFi event handlers before stopping,
    // otherwise esp_netif_destroy() can race with esp_wifi_stop()'s own netif teardown
    const bool release_netif = mode_bit == WIFI_MODE_STA && g_staOwnerCount <= 0 && g_staNetif != nullptr;
    if (release_netif) {
        esp_wifi_clear_default_wifi_driver_and_handlers(g_staNetif);
    }

    if (g_radioOwnerCount <= 0) {
        esp_wifi_stop();
        esp_wifi_set_mode(WIFI_MODE_NULL);
        esp_wifi_deinit();
        g_radioOwnerCount = 0;
        g_staOwnerCount = 0;
        g_apOwnerCount = 0;
        g_radioMode = WIFI_MODE_NULL;
    } else if (g_radioMode != previous_mode) {
        esp_wifi_set_mode(g_radioMode);
    }

    if (release_netif) {
        esp_netif_destroy(g_staNetif);
        g_staNetif = nullptr;
    }

    mutex_unlock(&g_radioMutex);
}

// Starting a netif twice asserts in lwip, and the co-processor can send WIFI_EVENT_STA_START more than once
// (e.g. when ESP-NOW and WiFi both start the radio). These wrappers make starting a netif idempotent.
// They are linked in with -Wl,--wrap in the top-level CMakeLists.txt.
extern void __real_esp_netif_action_start(void* esp_netif, esp_event_base_t base, int32_t event_id, void* data);
extern void __real_esp_netif_action_stop(void* esp_netif, esp_event_base_t base, int32_t event_id, void* data);
extern void __real_esp_netif_destroy(esp_netif_t* esp_netif);

void __wrap_esp_netif_action_start(void* esp_netif, esp_event_base_t base, int32_t event_id, void* data) {
    if (!mark_netif_started(esp_netif)) {
        LOG_W(TAG, "Ignoring a duplicate start of a netif");
        return;
    }
    __real_esp_netif_action_start(esp_netif, base, event_id, data);
}

void __wrap_esp_netif_action_stop(void* esp_netif, esp_event_base_t base, int32_t event_id, void* data) {
    mark_netif_stopped(esp_netif);
    __real_esp_netif_action_stop(esp_netif, base, event_id, data);
}

void __wrap_esp_netif_destroy(esp_netif_t* esp_netif) {
    mark_netif_stopped(esp_netif);
    __real_esp_netif_destroy(esp_netif);
}

Driver esp32_wifi_driver = {
    .name = "esp32_wifi",
    .compatible = (const char*[]) { "espressif,esp32-wifi", nullptr },
    .start_device = start_device,
    .stop_device = stop_device,
    .probe = nullptr,
    .api = static_cast<const void*>(&esp32_wifi_api),
    .device_type = &WIFI_TYPE,
    .internal = nullptr,
};

} // extern "C"

#endif // CONFIG_SOC_WIFI_SUPPORTED or CONFIG_ESP_HOSTED_ENABLED
