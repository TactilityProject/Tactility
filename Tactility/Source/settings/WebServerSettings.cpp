#include <Tactility/settings/WebServerSettings.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/file/File.h>
#include <Tactility/Logger.h>

#include <charconv>
#include <map>
#include <string>

#ifdef ESP_PLATFORM
#include <esp_mac.h>
#include <esp_wifi.h>
#endif

namespace tt::settings::webserver {

static const auto LOGGER = tt::Logger("WebServerSettings");
constexpr auto* SETTINGS_FILE = "/data/service/webserver/settings.properties";

// Property keys
constexpr auto* KEY_WIFI_ENABLED = "wifiEnabled";
constexpr auto* KEY_WIFI_MODE = "wifiMode";
constexpr auto* KEY_AP_SSID = "apSsid";
constexpr auto* KEY_AP_PASSWORD = "apPassword";
constexpr auto* KEY_AP_CHANNEL = "apChannel";
constexpr auto* KEY_WEBSERVER_ENABLED = "webServerEnabled";
constexpr auto* KEY_WEBSERVER_PORT = "webServerPort";
constexpr auto* KEY_WEBSERVER_AUTH_ENABLED = "webServerAuthEnabled";
constexpr auto* KEY_WEBSERVER_USERNAME = "webServerUsername";
constexpr auto* KEY_WEBSERVER_PASSWORD = "webServerPassword";

std::string generateDefaultApSsid() {
#ifdef ESP_PLATFORM
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return "Tactility-0000";
    }
    char ssid[16];
    snprintf(ssid, sizeof(ssid), "Tactility-%02X%02X", mac[4], mac[5]);
    return std::string(ssid);
#else
    return "Tactility-0000";
#endif
}

bool load(WebServerSettings& settings) {
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(SETTINGS_FILE, map)) {
        return false;
    }

    // Parse all settings from the map
    auto wifi_enabled = map.find(KEY_WIFI_ENABLED);
    auto wifi_mode = map.find(KEY_WIFI_MODE);
    auto ap_ssid = map.find(KEY_AP_SSID);
    auto ap_password = map.find(KEY_AP_PASSWORD);
    auto ap_channel = map.find(KEY_AP_CHANNEL);
    auto webserver_enabled = map.find(KEY_WEBSERVER_ENABLED);
    auto webserver_port = map.find(KEY_WEBSERVER_PORT);
    auto webserver_auth_enabled = map.find(KEY_WEBSERVER_AUTH_ENABLED);
    auto webserver_username = map.find(KEY_WEBSERVER_USERNAME);
    auto webserver_password = map.find(KEY_WEBSERVER_PASSWORD);

    // WiFi settings
    settings.wifiEnabled = (wifi_enabled != map.end())
        ? (wifi_enabled->second == "1" || wifi_enabled->second == "true")
        : false;  // Default disabled

    settings.wifiMode = (wifi_mode != map.end() && wifi_mode->second == "1")
        ? WiFiMode::AccessPoint
        : WiFiMode::Station;

    auto parseInt = [](const std::string& value, int min, int max, int fallback) -> int {
        int v = 0;
        auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), v);
        if (ec != std::errc{} || v < min || v > max) {
            return fallback;
        }
        return v;
    };

    // AP mode settings
    settings.apSsid = (ap_ssid != map.end()) ? ap_ssid->second : generateDefaultApSsid();
    settings.apPassword = (ap_password != map.end()) ? ap_password->second : "tactility";
    settings.apChannel = (ap_channel != map.end())
        ? static_cast<uint8_t>(parseInt(ap_channel->second, 1, 13, 1))
        : 1;

    // Web server settings
    settings.webServerEnabled = (webserver_enabled != map.end())
        ? (webserver_enabled->second == "1" || webserver_enabled->second == "true")
        : false;

    settings.webServerPort = (webserver_port != map.end())
        ? static_cast<uint16_t>(parseInt(webserver_port->second, 1, 65535, 80))
        : 80;

    // Web server auth settings
    settings.webServerAuthEnabled = (webserver_auth_enabled != map.end())
        ? (webserver_auth_enabled->second == "1" || webserver_auth_enabled->second == "true")
        : false;

    settings.webServerUsername = (webserver_username != map.end()) ? webserver_username->second : "admin";
    settings.webServerPassword = (webserver_password != map.end()) ? webserver_password->second : "admin";

    return true;
}

WebServerSettings getDefault() {
    return WebServerSettings{
        .wifiEnabled = false,  // Default WiFi OFF
        .wifiMode = WiFiMode::Station,
        .apSsid = generateDefaultApSsid(),
        .apPassword = "tactility",
        .apChannel = 1,
        .webServerEnabled = false,  // Default WebServer OFF for security
        .webServerPort = 80,
        .webServerAuthEnabled = false,
        .webServerUsername = "admin",
        .webServerPassword = "admin"
    };
}

WebServerSettings loadOrGetDefault() {
    WebServerSettings settings;

    bool loadedFromFlash = load(settings);
    if (!loadedFromFlash) {
        // First boot - use defaults (WiFi OFF, WebServer OFF)
        settings = getDefault();
        // Save defaults to flash so toggle states persist
        if (save(settings)) {
            LOGGER.info("First boot - saved default settings (WiFi OFF WebServer OFF)");
        } else {
            LOGGER.warn("First boot - failed to save default settings to flash");
        }
    }

    return settings;
}

bool save(const WebServerSettings& settings) {
    std::map<std::string, std::string> map;

    // WiFi settings
    map[KEY_WIFI_ENABLED] = settings.wifiEnabled ? "1" : "0";
    map[KEY_WIFI_MODE] = (settings.wifiMode == WiFiMode::AccessPoint) ? "1" : "0";

    // AP mode settings
    map[KEY_AP_SSID] = settings.apSsid;
    map[KEY_AP_PASSWORD] = settings.apPassword;
    map[KEY_AP_CHANNEL] = std::to_string(settings.apChannel);

    // Web server settings
    map[KEY_WEBSERVER_ENABLED] = settings.webServerEnabled ? "1" : "0";
    map[KEY_WEBSERVER_PORT] = std::to_string(settings.webServerPort);

    // Web server auth settings
    map[KEY_WEBSERVER_AUTH_ENABLED] = settings.webServerAuthEnabled ? "1" : "0";
    map[KEY_WEBSERVER_USERNAME] = settings.webServerUsername;
    map[KEY_WEBSERVER_PASSWORD] = settings.webServerPassword;

    // Save to flash storage only (no SD backup - settings sync at boot handles restore)
    return file::savePropertiesFile(SETTINGS_FILE, map);
}

}
