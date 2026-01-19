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
#include <esp_random.h>
#else
#include <random>
#endif

namespace tt::settings::webserver {

static const auto LOGGER = tt::Logger("WebServerSettings");
constexpr auto* SETTINGS_FILE = "/data/service/webserver/settings.properties";

// Property keys
constexpr auto* KEY_WIFI_ENABLED = "wifiEnabled";
constexpr auto* KEY_WIFI_MODE = "wifiMode";
constexpr auto* KEY_AP_SSID = "apSsid";
constexpr auto* KEY_AP_PASSWORD = "apPassword";
constexpr auto* KEY_AP_OPEN_NETWORK = "apOpenNetwork";
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
    snprintf(ssid, sizeof(ssid), "Tactility-%02X%02X", mac[2] ^ mac[3], mac[4] ^ mac[5]);
    return std::string(ssid);
#else
    return "Tactility-0000";
#endif
}

/**
 * @brief Generate a cryptographically secure random string for credentials
 * @param length The desired length of the string
 * @return A random alphanumeric string
 */
static std::string generateRandomCredential(size_t length) {
    static constexpr char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    static constexpr size_t charsetSize = sizeof(charset) - 1;

    std::string result;
    result.reserve(length);

#ifdef ESP_PLATFORM
    for (size_t i = 0; i < length; ++i) {
        uint32_t randomValue = esp_random();
        result += charset[randomValue % charsetSize];
    }
#else
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, charsetSize - 1);
    for (size_t i = 0; i < length; ++i) {
        result += charset[dis(gen)];
    }
#endif

    return result;
}

/**
 * @brief Check if a credential value is insecure (empty or the old "admin" default)
 * @param value The credential value to check
 * @return true if the credential is considered insecure
 */
static bool isInsecureCredential(const std::string& value) {
    return value.empty() || value == "admin" || value == "tactility";
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
    auto ap_open_network = map.find(KEY_AP_OPEN_NETWORK);
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
    settings.apSsid = (ap_ssid != map.end() && !ap_ssid->second.empty())
        ? ap_ssid->second
        : generateDefaultApSsid();
    settings.apPassword = (ap_password != map.end()) ? ap_password->second : "";
    settings.apOpenNetwork = (ap_open_network != map.end())
        ? (ap_open_network->second == "1" || ap_open_network->second == "true")
        : false;
    settings.apChannel = (ap_channel != map.end())
        ? static_cast<uint8_t>(parseInt(ap_channel->second, 1, 13, 1))
        : 1;

    // Security: If AP password is insecure (empty or the old "tactility" default),
    // generate a strong random password and persist it immediately.
    // Skip this if user explicitly wants an open network.
    if (!settings.apOpenNetwork && isInsecureCredential(settings.apPassword)) {
        LOGGER.warn("AP password is insecure - generating secure random password");

        // Generate 12-character random password (alphanumeric, ~71 bits of entropy)
        // WPA2 requires 8-63 characters, so 12 is well within range
        settings.apPassword = generateRandomCredential(12);

        // Persist the generated password immediately
        map[KEY_AP_PASSWORD] = settings.apPassword;
        if (file::savePropertiesFile(SETTINGS_FILE, map)) {
            LOGGER.info("Generated and saved new secure AP password");
        } else {
            LOGGER.error("Failed to save generated AP password");
        }
    }

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

    // Load credentials from file, defaulting to empty if not present
    settings.webServerUsername = (webserver_username != map.end()) ? webserver_username->second : "";
    settings.webServerPassword = (webserver_password != map.end()) ? webserver_password->second : "";

    // Security: If auth is enabled but credentials are insecure (empty or "admin"),
    // generate strong random credentials and persist them immediately
    if (settings.webServerAuthEnabled &&
        (isInsecureCredential(settings.webServerUsername) || isInsecureCredential(settings.webServerPassword))) {

        LOGGER.warn("Auth enabled with insecure credentials - generating secure random credentials");

        // Generate 12-character random credentials (alphanumeric, ~71 bits of entropy each)
        settings.webServerUsername = generateRandomCredential(12);
        settings.webServerPassword = generateRandomCredential(12);

        // Persist the generated credentials immediately
        // We need to save these to the file so they're consistent across reboots
        map[KEY_WEBSERVER_USERNAME] = settings.webServerUsername;
        map[KEY_WEBSERVER_PASSWORD] = settings.webServerPassword;
        if (file::savePropertiesFile(SETTINGS_FILE, map)) {
            LOGGER.info("Generated and saved new secure credentials");
        } else {
            LOGGER.error("Failed to save generated credentials - auth may be inconsistent across reboots");
        }
    }

    return true;
}

WebServerSettings getDefault() {
    return WebServerSettings{
        .wifiEnabled = false,  // Default WiFi OFF
        .wifiMode = WiFiMode::Station,
        .apSsid = generateDefaultApSsid(),
        .apPassword = "",  // Empty - will be auto-generated on first use (unless apOpenNetwork)
        .apOpenNetwork = false,  // Default to secured network
        .apChannel = 1,
        .webServerEnabled = false,  // Default WebServer OFF for security
        .webServerPort = 80,
        .webServerAuthEnabled = false,  // Auth disabled by default
        .webServerUsername = "",  // Empty - will be generated if auth is enabled
        .webServerPassword = ""   // Empty - will be generated if auth is enabled
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
    map[KEY_AP_OPEN_NETWORK] = settings.apOpenNetwork ? "1" : "0";
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
