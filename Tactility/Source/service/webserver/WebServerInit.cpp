#include <Tactility/TactilityCore.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/settings/WebServerSettings.h>
#include <Tactility/service/wifi/Wifi.h>
#include <Tactility/service/wifi/WifiApSettings.h>
#include <Tactility/service/webserver/WebServerService.h>
#include <Tactility/Logger.h>

#ifdef ESP_PLATFORM
#include <esp_wifi.h>
#include <esp_netif.h>
#include <lwip/ip4_addr.h>
#endif

namespace tt::service::webserver {

static const auto LOGGER = tt::Logger("WebServerInitService");

/**
 * @brief Service that initializes WiFi on boot
 * 
 * This service connects WiFi based on saved credentials.
 * The actual WebServer is handled by WebServerService.
 */
class WebServerInitService final : public Service {
private:
#ifdef ESP_PLATFORM
    esp_netif_t* ap_netif = nullptr;
    bool wifiInitialized = false;
#endif

public:
    bool onStart(TT_UNUSED ServiceContext& service) override {
        LOGGER.info("Loading web server settings...");
        
        auto settings = settings::webserver::loadOrGetDefault();
        
        LOGGER.info("WiFi Mode: {}", settings.wifiMode == settings::webserver::WiFiMode::Station ? "Station" : "AccessPoint");

        if (settings.wifiMode == settings::webserver::WiFiMode::AccessPoint) {
            LOGGER.info("AP SSID: {}", settings.apSsid);
        }
        
        LOGGER.info("WiFi Enabled: {}", settings.wifiEnabled ? "true" : "false");
        LOGGER.info("Web Server Enabled: {}", settings.webServerEnabled ? "true" : "false");
        LOGGER.info("Web Server Port: {}", settings.webServerPort);
        LOGGER.info("Web Server Auth: {}", settings.webServerAuthEnabled ? "enabled" : "disabled");

        // Only start WiFi if enabled in settings
        if (!settings.wifiEnabled) {
            LOGGER.info("WiFi disabled in settings - skipping WiFi initialization");
            return true;
        }

        // Connect WiFi based on mode
        // WebServer is handled by WebServerService which starts automatically if webServerEnabled=true
        if (settings.wifiMode == settings::webserver::WiFiMode::Station) {
            // Station mode - WiFi credentials are managed via WiFi menu (/data/settings/*.ap.properties)
            // Auto-connect happens via WiFi service, not here
            LOGGER.info("WiFi Station mode - auto-connect will use saved network credentials");
            LOGGER.info("Use WiFi menu to connect to networks (credentials saved to /data/settings/*.ap.properties)");
        } else {
            // Access Point mode
            LOGGER.info("Starting WiFi in Access Point mode...");

#ifdef ESP_PLATFORM
            // Initialize WiFi FIRST, before creating the network interface
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            if (esp_wifi_init(&cfg) != ESP_OK) {
                LOGGER.error("esp_wifi_init() failed");
                return false;
            }
            wifiInitialized = true;

            // Now create the AP network interface
            ap_netif = esp_netif_create_default_wifi_ap();
            if (ap_netif == nullptr) {
                LOGGER.error("esp_netif_create_default_wifi_ap() failed");
                esp_wifi_deinit();
                wifiInitialized = false;
                return false;
            }

            auto cleanup = [this]() {
                if (ap_netif != nullptr) {
                    esp_netif_destroy(ap_netif);
                    ap_netif = nullptr;
                }
                if (wifiInitialized) {
                    esp_wifi_deinit();
                    wifiInitialized = false;
                }
            };

            if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) {
                LOGGER.error("esp_wifi_set_mode() failed");
                cleanup();
                return false;
            }

            //init softap
            esp_netif_ip_info_t ip_info;
            memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
            ip_info.ip.addr = ipaddr_addr("192.168.4.1");
            ip_info.gw.addr = ipaddr_addr("192.168.4.1");
            ip_info.netmask.addr = ipaddr_addr("255.255.255.0");

            // Configure static IP for AP: 192.168.4.1/24
            if (esp_netif_dhcps_stop(ap_netif) != ESP_OK) {
                LOGGER.error("esp_netif_dhcps_stop() failed");
                cleanup();
                return false;
            }

            if (esp_netif_set_ip_info(ap_netif, &ip_info) != ESP_OK) {
                LOGGER.error("esp_netif_set_ip_info() failed");
                cleanup();
                return false;
            }

            if (esp_netif_dhcps_start(ap_netif) != ESP_OK) {
                LOGGER.error("esp_netif_dhcps_start() failed");
                cleanup();
                return false;
            }

            // Configure WiFi AP settings
            wifi_config_t wifi_config;
            memset(&wifi_config, 0, sizeof(wifi_config_t));
            strncpy(reinterpret_cast<char*>(wifi_config.ap.ssid), settings.apSsid.c_str(), sizeof(wifi_config.ap.ssid) - 1);
            wifi_config.ap.ssid[sizeof(wifi_config.ap.ssid) - 1] = '\0';
            if (settings.apPassword.length() >= 8 && settings.apPassword.length() <= 63) {
                wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
                strncpy(reinterpret_cast<char*>(wifi_config.ap.password), settings.apPassword.c_str(), sizeof(wifi_config.ap.password) - 1);
                wifi_config.ap.password[sizeof(wifi_config.ap.password) - 1] = '\0';
            } else {
                if (!settings.apPassword.empty()) {
                    LOGGER.warn("AP password invalid (must be 8-63 chars) - using OPEN mode");
                }
                wifi_config.ap.authmode = WIFI_AUTH_OPEN;
            }
            wifi_config.ap.max_connection = 4;
            wifi_config.ap.channel = settings.apChannel;

            if (esp_wifi_set_config(WIFI_IF_AP, &wifi_config) != ESP_OK) {
                LOGGER.error("esp_wifi_set_config() failed");
                cleanup();
                return false;
            }

            if (esp_wifi_start() != ESP_OK) {
                LOGGER.error("esp_wifi_start() failed");
                cleanup();
                return false;
            }

            LOGGER.info("WiFi AP started - SSID: {}, IP: 192.168.4.1", settings.apSsid);
#else
            LOGGER.warn("WiFi AP mode not available on simulator");
#endif
        }
        
        return true;
    }

    void onStop(TT_UNUSED ServiceContext& service) override {
#ifdef ESP_PLATFORM
        if (wifiInitialized) {
            esp_wifi_stop();
        }
        if (ap_netif != nullptr) {
            esp_netif_destroy(ap_netif);
            ap_netif = nullptr;
        }
        if (wifiInitialized) {
            esp_wifi_deinit();
            wifiInitialized = false;
        }
#endif
    }
};

extern const ServiceManifest webServerInitManifest = {
    .id = "WebServerInit",
    .createService = create<WebServerInitService>
};

}
