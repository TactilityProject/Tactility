#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/Logger.h>
#include <Tactility/service/espnow/EspNow.h>
#include <Tactility/service/wifi/Wifi.h>

#include <esp_now.h>
#include <esp_wifi.h>

namespace tt::service::espnow {

static const auto LOGGER = Logger("EspNowService");
static bool wifiStartedByEspNow = false;

bool initWifi(const EspNowConfig& config) {
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
            LOGGER.error("esp_wifi_init() failed");
            return false;
        }

        if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
            LOGGER.error("esp_wifi_set_storage() failed");
            return false;
        }

        if (esp_wifi_set_mode(mode) != ESP_OK) {
            LOGGER.error("esp_wifi_set_mode() failed");
            return false;
        }

        if (esp_wifi_start() != ESP_OK) {
            LOGGER.error("esp_wifi_start() failed");
            return false;
        }

        if (esp_wifi_set_channel(config.channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
            LOGGER.error("esp_wifi_set_channel() failed");
            return false;
        }
    }

    if (config.longRange) {
        wifi_interface_t wifi_interface;
        if (config.mode == Mode::Station) {
            wifi_interface = WIFI_IF_STA;
        } else {
            wifi_interface = WIFI_IF_AP;
        }

        if (esp_wifi_set_protocol(wifi_interface, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR) != ESP_OK) {
            LOGGER.warn("esp_wifi_set_protocol() for long range failed");
        }
    }

    LOGGER.info("WiFi initialized for ESP-NOW (wifi already running: {})", wifi_already_running ? "yes" : "no");
    return true;
}

bool deinitWifi() {
    if (wifiStartedByEspNow) {
        esp_wifi_stop();
        esp_wifi_deinit();
        wifiStartedByEspNow = false;
        LOGGER.info("WiFi stopped (was started by ESP-NOW)");
    } else {
        LOGGER.info("WiFi left running (managed by WiFi service)");
    }
    return true;
}

} // namespace tt::service::espnow

#endif // CONFIG_SOC_WIFI_SUPPORTED || CONFIG_SLAVE_SOC_WIFI_SUPPORTED
