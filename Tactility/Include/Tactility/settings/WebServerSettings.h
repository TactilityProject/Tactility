#pragma once

#include <cstdint>
#include <string>

namespace tt::settings::webserver {

enum class WiFiMode : uint8_t {
    Station = 0,  // Connect to existing WiFi network
    AccessPoint = 1  // Create own WiFi network
};

struct WebServerSettings {
    // WiFi Configuration
    bool wifiEnabled = false;    // Enable/disable WiFi entirely
    WiFiMode wifiMode = WiFiMode::Station;          // Station or Access Point

    // Access Point Mode Settings
    std::string apSsid{};           // Default: "Tactility-XXXX" (last 4 of MAC)
    std::string apPassword{};       // Password for WPA2 (8-63 chars)
    bool apOpenNetwork = false;     // If true, create open network (no password)
    uint8_t apChannel = 1;            // 1-13

    // Web Server Settings
    bool webServerEnabled = false;
    uint16_t webServerPort = 80;       // Default: 80

    // Optional HTTP Basic Auth
    bool webServerAuthEnabled = false;
    std::string webServerUsername{};
    std::string webServerPassword{};
};

/**
 * @brief Load web server settings from persistent storage
 * @param[out] settings The settings structure to populate
 * @return true if settings were loaded successfully, false otherwise
 */
bool load(WebServerSettings& settings);

/**
 * @brief Get default web server settings
 * @return Default settings structure
 */
WebServerSettings getDefault();

/**
 * @brief Load settings or return defaults if loading fails
 * @return Settings structure (either loaded or default)
 */
WebServerSettings loadOrGetDefault();

/**
 * @brief Save web server settings to persistent storage
 * @param[in] settings The settings to save
 * @return true if settings were saved successfully, false otherwise
 */
bool save(const WebServerSettings& settings);

/**
 * @brief Generate default AP SSID based on device MAC address
 * @return SSID string in format "Tactility-XXXX"
 */
std::string generateDefaultApSsid();

/**
 * @brief Generate a cryptographically secure random string for credentials
 * @param length The desired length of the string
 * @return A random alphanumeric string
 */
std::string generateRandomCredential(size_t length);

}
