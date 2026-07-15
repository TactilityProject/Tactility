#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/service/espnow/EspNowBackend.h>
#include <Tactility/service/wifi/Wifi.h>

#include <esp_now.h>
#include <esp_wifi.h>

#include <tactility/log.h>

namespace tt::service::espnow::backend {

constexpr auto* TAG = "EspNowBackendNative";
static bool wifiStartedByEspNow = false;
static bool longRangeProtocolApplied = false;
static wifi_interface_t longRangeInterface = WIFI_IF_STA;
static uint8_t savedProtocolBitmap = 0;

static bool deinitWifi();

static bool initWifi(const EspNowConfig& config) {
    auto wifi_state = wifi::getRadioState();
    bool wifi_already_running = (wifi_state != wifi::RadioState::Off && wifi_state != wifi::RadioState::OffPending);

    wifi_mode_t mode;
    if (config.mode == Mode::Station) {
        mode = wifi_mode_t::WIFI_MODE_STA;
    } else {
        mode = wifi_mode_t::WIFI_MODE_AP;
    }

    // Only initialize WiFi if it's not already running; ESP-NOW coexists with STA mode
    wifiStartedByEspNow = !wifi_already_running;
    if (wifiStartedByEspNow) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&cfg) != ESP_OK) {
            LOG_E(TAG,"esp_wifi_init() failed");
            wifiStartedByEspNow = false;
            return false;
        }

        if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
            LOG_E(TAG,"esp_wifi_set_storage() failed");
            esp_wifi_deinit();
            wifiStartedByEspNow = false;
            return false;
        }

        if (esp_wifi_set_mode(mode) != ESP_OK) {
            LOG_E(TAG,"esp_wifi_set_mode() failed");
            esp_wifi_deinit();
            wifiStartedByEspNow = false;
            return false;
        }

        if (esp_wifi_start() != ESP_OK) {
            LOG_E(TAG,"esp_wifi_start() failed");
            esp_wifi_deinit();
            wifiStartedByEspNow = false;
            return false;
        }

        if (config.channel != 0 && esp_wifi_set_channel(config.channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
            LOG_E(TAG,"esp_wifi_set_channel() failed");
            deinitWifi();
            return false;
        }
    } else if (config.channel != 0 &&
            wifi_state != wifi::RadioState::ConnectionActive && wifi_state != wifi::RadioState::ConnectionPending) {
        // WifiService already owns the radio but isn't associated to an AP: an unassociated STA's
        // operating channel is otherwise left undefined/wherever it last scanned to, which silently
        // breaks ESP-NOW (esp_now_send() reports success but nothing reaches a peer sitting on a
        // different channel) - only skip this when actually connected/connecting, where the STA is
        // already locked to the AP's channel and forcing a different one would disrupt that link.
        if (esp_wifi_set_channel(config.channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
            LOG_W(TAG, "esp_wifi_set_channel() failed on externally managed radio - ESP-NOW may not reach peers");
        }
    }

    if (config.longRange) {
        wifi_interface_t wifi_interface = (config.mode == Mode::Station) ? WIFI_IF_STA : WIFI_IF_AP;

        // Preserve whatever protocol mask the interface already had (e.g. set by WifiService if
        // it owns this interface) so it can be restored in deinitWifi() - esp_wifi_set_protocol()
        // below otherwise permanently stomps it, even when ESP-NOW doesn't own the radio.
        uint8_t previousBitmap = 0;
        bool hadPreviousBitmap = esp_wifi_get_protocol(wifi_interface, &previousBitmap) == ESP_OK;

        if (esp_wifi_set_protocol(wifi_interface, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR) != ESP_OK) {
            LOG_W(TAG,"esp_wifi_set_protocol() for long range failed");
        } else if (hadPreviousBitmap) {
            longRangeProtocolApplied = true;
            longRangeInterface = wifi_interface;
            savedProtocolBitmap = previousBitmap;
        }
    }

    LOG_I(TAG, "WiFi initialized for ESP-NOW (wifi already running: %s)", wifi_already_running ? "yes" : "no");
    return true;
}

static bool deinitWifi() {
    // Restore the interface's protocol mask before either stopping WiFi or handing the radio
    // back to WifiService - covers both paths, since an externally managed radio (wifiStartedByEspNow
    // == false) is the case that actually needs its prior protocol bits put back.
    if (longRangeProtocolApplied) {
        if (esp_wifi_set_protocol(longRangeInterface, savedProtocolBitmap) != ESP_OK) {
            LOG_W(TAG, "Failed to restore prior WiFi protocol bitmap");
        }
        longRangeProtocolApplied = false;
    }

    if (wifiStartedByEspNow) {
        esp_wifi_stop();
        esp_wifi_deinit();
        wifiStartedByEspNow = false;
        LOG_I(TAG, "WiFi stopped (was started by ESP-NOW)");
    } else {
        LOG_I(TAG, "WiFi left running (managed by WiFi service)");
    }
    return true;
}

static uint32_t espnowVersion = 0;

bool init(const EspNowConfig& config, esp_now_recv_cb_t recvCallback) {
    if (!initWifi(config)) {
        LOG_E(TAG, "initWifi() failed");
        return false;
    }

    if (esp_now_init() != ESP_OK) {
        LOG_E(TAG, "esp_now_init() failed");
        deinitWifi();
        return false;
    }

    if (esp_now_register_recv_cb(recvCallback) != ESP_OK) {
        LOG_E(TAG, "esp_now_register_recv_cb() failed");
        esp_now_deinit();
        deinitWifi();
        return false;
    }

    if (esp_now_set_pmk(config.masterKey) != ESP_OK) {
        LOG_E(TAG, "esp_now_set_pmk() failed");
        esp_now_deinit();
        deinitWifi();
        return false;
    }

    espnowVersion = 0;
    if (esp_now_get_version(&espnowVersion) == ESP_OK) {
        LOG_I(TAG, "ESP-NOW version: %u.0", (unsigned)espnowVersion);
    } else {
        LOG_W(TAG, "Failed to get ESP-NOW version");
    }

    return true;
}

bool deinit() {
    bool success = true;

    if (esp_now_deinit() != ESP_OK) {
        LOG_E(TAG, "esp_now_deinit() failed");
        success = false;
    }

    if (!deinitWifi()) {
        LOG_E(TAG, "deinitWifi() failed");
        success = false;
    }

    espnowVersion = 0;
    return success;
}

bool addPeer(const esp_now_peer_info_t& peer) {
    if (esp_now_add_peer(&peer) != ESP_OK) {
        LOG_E(TAG, "Failed to add peer");
        return false;
    } else {
        LOG_I(TAG, "Peer added");
        return true;
    }
}

bool send(const uint8_t* address, const uint8_t* buffer, size_t bufferLength) {
    return esp_now_send(address, buffer, bufferLength) == ESP_OK;
}

uint32_t getVersion() {
    return espnowVersion;
}

}

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
