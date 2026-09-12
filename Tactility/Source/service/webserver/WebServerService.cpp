#include <Tactility/service/webserver/WebServerService.h>
#include <Tactility/service/ServiceManifest.h>

#include <app/start.h>
#include <Tactility/settings/WebServerSettings.h>
#include <Tactility/MountPoints.h>
#include <Tactility/file/File.h>
#include <Tactility/lvgl/Statusbar.h>
#include <Tactility/Mutex.h>

#include <Tactility/DeprecatedPaths.h>
#include <Tactility/StringUtils.h>
#include <Tactility/TactilityConfig.h>
#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/network/HttpServerReq.h>
#include <Tactility/network/HttpdReq.h>
#include <Tactility/network/Url.h>
#include <Tactility/service/wifi/Wifi.h>

#include <tactility/check.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/log.h>

#include <lvgl/lvgl.h>
#include <lvgl/icons/statusbar.h>

#if TT_FEATURE_SCREENSHOT_ENABLED
#include <lv_screenshot.h>
#endif

#include "app/install.h"
#include "app/manager.h"

#ifdef ESP_PLATFORM
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_vfs_fat.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#include <tactility/drivers/esp32_wifi_radio.h>
#include <lwip/ip4_addr.h>
#endif

#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <format>
#include <iomanip>
#include <iterator>
#include <mbedtls/base64.h>
#include <sstream>
#include <vector>

namespace tt::service::webserver {

constexpr auto* TAG = "WebServerService";

#ifdef ESP_PLATFORM
// Helper to convert chip model enum to human-readable string
static const char* getChipModelName(esp_chip_model_t model) {
    switch (model) {
        case CHIP_ESP32:   return "ESP32";
        case CHIP_ESP32S2: return "ESP32-S2";
        case CHIP_ESP32S3: return "ESP32-S3";
        case CHIP_ESP32C3: return "ESP32-C3";
        case CHIP_ESP32C2: return "ESP32-C2";
        case CHIP_ESP32C6: return "ESP32-C6";
        case CHIP_ESP32H2: return "ESP32-H2";
        case CHIP_ESP32P4: return "ESP32-P4";
        case CHIP_ESP32C5: return "ESP32-C5";
        case CHIP_ESP32C61: return "ESP32-C61";
        default:           return "Unknown";
    }
}
#endif

// Cached settings to avoid SD card reads on every HTTP request
static Mutex g_settingsMutex;
static settings::webserver::WebServerSettings g_cachedSettings;
static bool g_settingsCached = false;

// Global instance pointer for controlling the service (atomic to prevent TOCTOU races)
static std::atomic<WebServerService*> g_webServerInstance{nullptr};

constexpr int MAX_UPLOAD_SIZE = 10 * 1024 * 1024; // 10 MB limit

static void publish_event(WebServerService* webserver, WebServerEvent event) {
    webserver->getPubsub()->publish(event);
}

std::shared_ptr<PubSub<WebServerEvent>> getPubsub() {
    WebServerService* webserver = g_webServerInstance.load();
    if (webserver == nullptr) {
        check(false, "Service not running");
    }

    return webserver->getPubsub();
}

static bool secureCompare(const std::string& a, const std::string& b) {
    size_t maxLen = std::max(a.size(), b.size());
    volatile unsigned char result = 0;
    result |= (a.size() != b.size());
    for (size_t i = 0; i < maxLen; ++i) {
        unsigned char ca = (i < a.size()) ? static_cast<unsigned char>(a[i]) : 0;
        unsigned char cb = (i < b.size()) ? static_cast<unsigned char>(b[i]) : 0;
        result |= ca ^ cb;
    }
    return result == 0;
}

// Helper to send 401 Unauthorized response with WWW-Authenticate header
static error_t sendUnauthorized(HttpServerRequest* request, const char* message) {
    http_server_request_set_header(request, "WWW-Authenticate", "Basic realm=\"Tactility\"");
    http_server_request_send_error(request, 401, message);
    return ERROR_NONE;  // Response was sent successfully
}

// Helper to validate HTTP Basic Auth on sensitive endpoints
// Returns ERROR_NONE with authPassed=true if auth succeeded or is disabled
// Returns ERROR_NONE with authPassed=false if auth failed (401 response already sent)
static error_t validateRequestAuth(HttpServerRequest* request, bool& authPassed) {
    authPassed = false;

    // Copy settings under lock to avoid race with settings update callback
    settings::webserver::WebServerSettings settings;
    {
        auto lock = g_settingsMutex.asScopedLock();
        lock.lock();
        settings = g_cachedSettings;
    }

    if (!settings.webServerAuthEnabled) {
        authPassed = true;
        return ERROR_NONE;  // Auth disabled, allow request
    }

    // Get Authorization header
    size_t auth_len = http_server_request_get_header(request, "Authorization", nullptr, 0);
    if (auth_len == 0) {
        return sendUnauthorized(request, "Authorization required");
    }
    std::string auth_header(auth_len, '\0');
    http_server_request_get_header(request, "Authorization", auth_header.data(), auth_len + 1);

    // Check for "Basic " prefix
    if (auth_header.rfind("Basic ", 0) != 0) {
        LOG_W(TAG, "Authorization header is not Basic auth");
        return sendUnauthorized(request, "Basic authorization required");
    }

    // Extract base64 encoded credentials
    std::string base64_creds = auth_header.substr(6);

    // Decode base64 using mbedtls (available in ESP-IDF)
    size_t decoded_len = 0;
    // First pass to get length
    mbedtls_base64_decode(nullptr, 0, &decoded_len,
                          reinterpret_cast<const unsigned char*>(base64_creds.c_str()),
                          base64_creds.length());

    std::string decoded(decoded_len, '\0');
    size_t actual_len = 0;
    int ret = mbedtls_base64_decode(reinterpret_cast<unsigned char*>(decoded.data()),
                                     decoded_len, &actual_len,
                                     reinterpret_cast<const unsigned char*>(base64_creds.c_str()),
                                     base64_creds.length());
    if (ret != 0) {
        LOG_W(TAG, "Failed to decode base64 credentials");
        return sendUnauthorized(request, "Invalid credentials format");
    }
    decoded.resize(actual_len);

    // Parse username:password
    size_t colon_pos = decoded.find(':');
    if (colon_pos == std::string::npos) {
        LOG_W(TAG, "Invalid credentials format (no colon separator)");
        return sendUnauthorized(request, "Invalid credentials format");
    }

    std::string username = decoded.substr(0, colon_pos);
    std::string password = decoded.substr(colon_pos + 1);

    // Validate against cached settings
    bool usernameMatch = secureCompare(username, settings.webServerUsername);
    bool passwordMatch = secureCompare(password, settings.webServerPassword);
    if (!usernameMatch || !passwordMatch) {
        LOG_W(TAG, "Invalid credentials for user '%s'", username.c_str());
        return sendUnauthorized(request, "Invalid credentials");
    }

    authPassed = true;
    return ERROR_NONE;  // Auth successful
}

bool WebServerService::onStart(ServiceContext& service) {
    LOG_I(TAG, "Starting WebServer service...");

    // Register global instance
    g_webServerInstance.store(this);

    // Create statusbar icon (hidden initially, shown when server actually starts)
    statusbarIconId = lvgl::statusbar_icon_add();
    lvgl::statusbar_icon_set_visibility(statusbarIconId, false);

    // Load and cache settings once at boot
    bool serverEnabled;
    {
        auto lock = g_settingsMutex.asScopedLock();
        lock.lock();
        g_cachedSettings = settings::webserver::loadOrGetDefault();
        g_settingsCached = true;
        serverEnabled = g_cachedSettings.webServerEnabled;
    }
    // Subscribe to settings change events to refresh cache
    settingsEventSubscription = pubsub->subscribe([](WebServerEvent event) {
        if (event == WebServerEvent::WebServerSettingsChanged) {
            auto lock = g_settingsMutex.asScopedLock();
            lock.lock();
            g_cachedSettings = settings::webserver::loadOrGetDefault();
            g_settingsCached = true;
        }
    });

    // Start HTTP server only if enabled in settings (default: OFF to save memory)
    if (serverEnabled) {
        LOG_I(TAG, "WebServer enabled in settings, starting HTTP server...");
        setEnabled(true);
    } else {
        LOG_I(TAG, "WebServer disabled in settings, NOT starting HTTP server (saves ~10KB RAM)");
        setEnabled(false);
    }

    return true;
}

void WebServerService::onStop(ServiceContext& service) {
    g_webServerInstance.store(nullptr);

    pubsub->unsubscribe(settingsEventSubscription);
    settingsEventSubscription = 0;

    setEnabled(false);

    // Remove statusbar icon
    if (statusbarIconId >= 0) {
        lvgl::statusbar_icon_remove(statusbarIconId);
        statusbarIconId = -1;
    }
}

// region Enable/Disable

void WebServerService::setEnabled(bool enabled) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    
    if (enabled) {
        if (httpServer == nullptr) {
            startServer();
        }
    } else {
        if (httpServer != nullptr) {
            stopServer();
        }
    }
}

bool WebServerService::isEnabled() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return httpServer != nullptr;
}

// region AP Mode WiFi Management

#ifdef ESP_PLATFORM

bool WebServerService::startApMode() {
    // Copy settings locally
    settings::webserver::WebServerSettings settings;
    {
        auto lock = g_settingsMutex.asScopedLock();
        lock.lock();
        settings = g_cachedSettings;
    }

    if (settings.wifiMode != settings::webserver::WiFiMode::AccessPoint) {
        LOG_I(TAG, "Not in AP mode, skipping AP WiFi initialization");
        return true;  // Not an error, just not needed
    }

    LOG_I(TAG, "Starting WiFi in Access Point mode...");

    // Acquires the shared radio (see esp32_wifi_radio.h): merges into WIFI_MODE_APSTA if Station
    // mode already owns the radio, instead of calling esp_wifi_init()/start() ourselves and
    // colliding with it.
    if (!esp32_wifi_radio_acquire(WIFI_MODE_AP)) {
        LOG_E(TAG, "Failed to acquire WiFi radio for AP mode");
        return false;
    }
    apWifiInitialized = true;

    // Create the AP network interface
    apNetif = esp_netif_create_default_wifi_ap();
    if (apNetif == nullptr) {
        LOG_E(TAG, "esp_netif_create_default_wifi_ap() failed");
        stopApMode();
        return false;
    }

    // Configure static IP for AP: 192.168.4.1/24
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
    ip_info.ip.addr = ipaddr_addr("192.168.4.1");
    ip_info.gw.addr = ipaddr_addr("192.168.4.1");
    ip_info.netmask.addr = ipaddr_addr("255.255.255.0");

    if (esp_netif_dhcps_stop(apNetif) != ESP_OK) {
        LOG_E(TAG, "esp_netif_dhcps_stop() failed");
        stopApMode();
        return false;
    }

    if (esp_netif_set_ip_info(apNetif, &ip_info) != ESP_OK) {
        LOG_E(TAG, "esp_netif_set_ip_info() failed");
        stopApMode();
        return false;
    }

    if (esp_netif_dhcps_start(apNetif) != ESP_OK) {
        LOG_E(TAG, "esp_netif_dhcps_start() failed");
        stopApMode();
        return false;
    }

    // Configure WiFi AP settings
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));

    // Set SSID
    strncpy(reinterpret_cast<char*>(wifi_config.ap.ssid), settings.apSsid.c_str(), sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid[sizeof(wifi_config.ap.ssid) - 1] = '\0';
    wifi_config.ap.ssid_len = static_cast<uint8_t>(settings.apSsid.length());

    // Set password and auth mode
    if (settings.apOpenNetwork) {
        // User explicitly chose an open network
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        LOG_I(TAG, "AP configured with OPEN authentication (user choice)");
    } else if (settings.apPassword.length() >= 8 && settings.apPassword.length() <= 63) {
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        strncpy(reinterpret_cast<char*>(wifi_config.ap.password), settings.apPassword.c_str(), sizeof(wifi_config.ap.password) - 1);
        wifi_config.ap.password[sizeof(wifi_config.ap.password) - 1] = '\0';
        LOG_I(TAG, "AP configured with WPA2-PSK authentication");
    } else {
        if (!settings.apPassword.empty()) {
            LOG_W(TAG, "AP password invalid (must be 8-63 chars, got %zu) - using OPEN mode", settings.apPassword.length());
        }
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        LOG_W(TAG, "AP configured with OPEN authentication (no password)");
    }

    wifi_config.ap.max_connection = 4;
    wifi_config.ap.channel = settings.apChannel;

    if (esp_wifi_set_config(WIFI_IF_AP, &wifi_config) != ESP_OK) {
        LOG_E(TAG, "esp_wifi_set_config(AP) failed");
        stopApMode();
        return false;
    }

    LOG_I(TAG, "WiFi AP started - SSID: '%s', Channel: %u, IP: 192.168.4.1", settings.apSsid.c_str(), (unsigned)settings.apChannel);
    return true;
}

void WebServerService::stopApMode() {
    if (apWifiInitialized) {
        // Detach netif from the internal WiFi event handlers before releasing the radio, otherwise
        // esp_netif_destroy() can race with esp_wifi_stop()'s own netif teardown (matches the same
        // guard in esp32_wifi.cpp's bring_down_wifi()).
        if (apNetif != nullptr) {
            esp_wifi_clear_default_wifi_driver_and_handlers(apNetif);
        }
        esp32_wifi_radio_release(WIFI_MODE_AP);
        LOG_I(TAG, "WiFi AP stopped");
        apWifiInitialized = false;
    }

    if (apNetif != nullptr) {
        esp_netif_destroy(apNetif);
        apNetif = nullptr;
    }
}

#else

bool WebServerService::startApMode() {
    LOG_W(TAG, "AP mode WiFi is not supported on this platform");
    return false;
}

void WebServerService::stopApMode() {}

#endif // ESP_PLATFORM

// endregion

bool WebServerService::startServer() {
    // Copy settings locally to minimize lock duration
    settings::webserver::WebServerSettings settings;
    {
        auto lock = g_settingsMutex.asScopedLock();
        lock.lock();
        settings = g_cachedSettings;
    }

    // Start AP mode WiFi if configured
    if (settings.wifiMode == settings::webserver::WiFiMode::AccessPoint) {
        if (!startApMode()) {
            LOG_E(TAG, "Failed to start AP mode WiFi - HTTP server will not start");
            return false;
        }
    }

    // NOTE: If you see 'no slots left for registering handler', increase CONFIG_HTTPD_MAX_URI_HANDLERS in sdkconfig (default is 8, 16+ recommended for many endpoints)
    void* ctx = this;  // Avoid IDE warnings about 'this' in designated initializers
    HttpServerRequestHandler handlers[] = {
        {
            .uri       = "/",
            .method    = HTTP_METHOD_GET,
            .callback  = handleRoot,
            .user_ctx  = ctx
        },
        // Note: /upload removed in favor of POST /fs/upload handled by /fs/* dispatcher
        {
            .uri       = "/filebrowser",
            .method    = HTTP_METHOD_GET,
            .callback  = handleFileBrowser,
            .user_ctx  = ctx
        },
        // Consolidated /fs/* handlers (dispatch internally) to save uri handler slots
        {
            .uri       = "/fs/*",
            .method    = HTTP_METHOD_GET,
            .callback  = handleFsGenericGet,
            .user_ctx  = ctx
        },
        {
            .uri       = "/fs/*",
            .method    = HTTP_METHOD_POST,
            .callback  = handleFsGenericPost,
            .user_ctx  = ctx
        },
        // Consolidated admin POST endpoints to save handler slots
        {
            .uri       = "/admin/*",
            .method    = HTTP_METHOD_POST,
            .callback  = handleAdminPost,
            .user_ctx  = ctx
        },
        // API endpoints for system info, apps, wifi, etc
        {
            .uri       = "/api/*",
            .method    = HTTP_METHOD_GET,
            .callback  = handleApiGet,
            .user_ctx  = ctx
        },
        {
            .uri       = "/api/*",
            .method    = HTTP_METHOD_POST,
            .callback  = handleApiPost,
            .user_ctx  = ctx
        },
        {
            .uri       = "/api/*",
            .method    = HTTP_METHOD_PUT,
            .callback  = handleApiPut,
            .user_ctx  = ctx
        },
        {
            .uri       = "/*",  // Catch-all for dynamic assets
            .method    = HTTP_METHOD_GET,
            .callback  = handleAssets,
            .user_ctx  = ctx
        }
    };

    HttpServerConfig config {
        .port = settings.webServerPort,
        .address = "0.0.0.0",
        .stack_size = 8192,
        .handlers = handlers,
        .handler_count = std::size(handlers),
    };

    httpServer = http_server_alloc(&config);
    if (httpServer == nullptr || http_server_start(httpServer) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start HTTP server on port %u", (unsigned)settings.webServerPort);
        http_server_free(httpServer);
        httpServer = nullptr;
        stopApMode();
        return false;
    }

    LOG_I(TAG, "HTTP server started successfully on port %u", (unsigned)settings.webServerPort);
    publish_event(this, WebServerEvent::WebServerStarted);

    // Show statusbar icon
    if (statusbarIconId >= 0) {
        lvgl::statusbar_icon_set_image(statusbarIconId, LVGL_ICON_STATUSBAR_CLOUD);
        lvgl::statusbar_icon_set_visibility(statusbarIconId, true);
        LOG_I(TAG, "WebServer statusbar icon shown (%s mode)",
                 settings.wifiMode == settings::webserver::WiFiMode::AccessPoint ? "AP" : "Station");
    }

    return true;
}

void WebServerService::stopServer() {
    if (httpServer == nullptr) {
        return;
    }

    http_server_free(httpServer);
    httpServer = nullptr;

    // Stop AP mode WiFi if we started it
    if (apWifiInitialized
#ifdef ESP_PLATFORM
        || apNetif != nullptr
#endif
    ) {
        stopApMode();
    }

    LOG_I(TAG, "HTTP server stopped");
    publish_event(this, WebServerEvent::WebServerStopped);

    if (statusbarIconId >= 0) {
        lvgl::statusbar_icon_set_visibility(statusbarIconId, false);
    }
}

WebServerService::~WebServerService() {
    http_server_free(httpServer);
}

// region Endpoints

error_t WebServerService::handleRoot(HttpServerRequest* request, void*) {
    LOG_I(TAG, "GET / -> redirecting to /dashboard.html");
    http_server_request_set_status(request, 302);
    http_server_request_set_header(request, "Location", "/dashboard.html");
    return http_server_request_send(request, nullptr, 0);
}

// region File Browser helpers & handlers

// Helper to determine content type from file extension
static const char* getContentType(const std::string& path) {
    // Check from the end to avoid matching extensions in directory names
    auto endsWith = [&path](const char* ext) {
        size_t extLen = strlen(ext);
        return path.length() >= extLen &&
               path.compare(path.length() - extLen, extLen, ext) == 0;
    };

    // HTML/Text
    if (endsWith(".html") || endsWith(".htm")) return "text/html";
    if (endsWith(".css")) return "text/css";
    if (endsWith(".js")) return "application/javascript";
    if (endsWith(".json")) return "application/json";
    if (endsWith(".xml")) return "application/xml";
    if (endsWith(".txt")) return "text/plain";

    // Images
    if (endsWith(".png")) return "image/png";
    if (endsWith(".jpg") || endsWith(".jpeg")) return "image/jpeg";
    if (endsWith(".gif")) return "image/gif";
    if (endsWith(".svg")) return "image/svg+xml";
    if (endsWith(".ico")) return "image/x-icon";
    if (endsWith(".webp")) return "image/webp";

    // Fonts
    if (endsWith(".woff")) return "font/woff";
    if (endsWith(".woff2")) return "font/woff2";
    if (endsWith(".ttf")) return "font/ttf";
    if (endsWith(".otf")) return "font/otf";
    if (endsWith(".eot")) return "application/vnd.ms-fontobject";

    // Audio/Video
    if (endsWith(".mp3")) return "audio/mpeg";
    if (endsWith(".wav")) return "audio/wav";
    if (endsWith(".ogg")) return "audio/ogg";
    if (endsWith(".mp4")) return "video/mp4";
    if (endsWith(".webm")) return "video/webm";

    // Archives/Documents
    if (endsWith(".pdf")) return "application/pdf";
    if (endsWith(".zip")) return "application/zip";
    if (endsWith(".gz")) return "application/gzip";

    // Default
    return "application/octet-stream";
}

static bool isAllowedBasePath(const std::string& path, bool allowRoot = false) {
    if (path.empty()) return false;
    // Check for ".." as a complete path component
    if (path == ".." || path.starts_with("../") ||
        path.find("/../") != std::string::npos || path.ends_with("/..")) {
        return false;
    }
    if (allowRoot && path == "/") return true;
    return path.starts_with("/data") || path.starts_with("/system/app/WebServer") || path.starts_with("/sdcard");
}

// Normalize client-supplied path: URL-decode, trim quotes/control chars, ensure leading slash, collapse duplicate slashes
static std::string normalizePath(const std::string& raw) {
    // Helper: hex to int
    auto hexVal = [](char c)->int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };

    std::string s = raw;
    // Remove surrounding single or double quotes
    if (s.size() >= 2 && ((s.front() == '\'' && s.back() == '\'') || (s.front() == '"' && s.back() == '"'))) {
        s = s.substr(1, s.size() - 2);
    }

    // URL-decode: %xx and '+' -> ' '
    std::string decoded;
    decoded.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '%') {
            if (i + 2 < s.size()) {
                int hi = hexVal(s[i+1]);
                int lo = hexVal(s[i+2]);
                if (hi >= 0 && lo >= 0) {
                    decoded.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
            // malformed %, keep it
            decoded.push_back(c);
        } else if (c == '+') {
            decoded.push_back(' ');
        } else {
            // strip control characters
            if (static_cast<unsigned char>(c) > 31) decoded.push_back(c);
        }
    }

    // Trim whitespace from ends
    size_t start = 0;
    while (start < decoded.size() && isspace((unsigned char)decoded[start])) ++start;
    size_t end = decoded.size();
    while (end > start && isspace((unsigned char)decoded[end-1])) --end;
    std::string trimmed = decoded.substr(start, end - start);

    // Ensure leading slash
    if (!trimmed.empty() && trimmed.front() != '/') trimmed = '/' + trimmed;
    if (trimmed.empty()) trimmed = "/";

    // Collapse duplicate slashes
    std::string out;
    out.reserve(trimmed.size());
    bool lastSlash = false;
    for (char c : trimmed) {
        if (c == '/') {
            if (!lastSlash) { out.push_back(c); lastSlash = true; }
        } else { out.push_back(c); lastSlash = false; }
    }

    return out;
}

static std::string escapeJson(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

// Raw (not URL-decoded) extraction, matching ESP-IDF's httpd_query_key_value() semantics -
// callers that need decoding (e.g. normalizePath()) already do it themselves on the raw value.
static bool getQueryParam(HttpServerRequest* request, const char* key, std::string& out) {
    size_t length = http_server_request_get_query(request, nullptr, 0);
    if (length == 0) {
        return false;
    }
    std::string query(length, '\0');
    http_server_request_get_query(request, query.data(), length + 1);

    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        size_t pair_end = amp == std::string::npos ? query.size() : amp;
        size_t eq = query.find('=', pos);
        if (eq != std::string::npos && eq < pair_end && query.compare(pos, eq - pos, key) == 0) {
            out = query.substr(eq + 1, pair_end - eq - 1);
            return true;
        }
        if (amp == std::string::npos) {
            break;
        }
        pos = amp + 1;
    }
    return false;
}

static bool uriMatches(const char* uri, const char* route) {
    const size_t n = strlen(route);
    return strncmp(uri, route, n) == 0 && (uri[n] == '\0' || uri[n] == '?' || uri[n] == '/');
}

error_t WebServerService::handleFileBrowser(HttpServerRequest* request, void*) {
    LOG_I(TAG, "GET /filebrowser -> redirecting to /dashboard.html#files");
    http_server_request_set_status(request, 302);
    http_server_request_set_header(request, "Location", "/dashboard.html#files");
    return http_server_request_send(request, nullptr, 0);
}

error_t WebServerService::handleFsList(HttpServerRequest* request, void*) {
    std::string path;
    // Log raw query string for diagnostics
    char qbuf[256];
    if (http_server_request_get_query(request, qbuf, sizeof(qbuf)) > 0) {
        LOG_I(TAG, "GET /fs/list raw query: %s", qbuf);
    }

    if (!getQueryParam(request, "path", path) || path.empty()) path = "/";
    std::string norm = normalizePath(path);
    LOG_I(TAG, "GET /fs/list decoded path: '%s' normalized: '%s'", path.c_str(), norm.c_str());

    // Allow root path for listing mount points
    if (!isAllowedBasePath(norm, true)) {
        LOG_W(TAG, "GET /fs/list - invalid path requested: '%s' normalized: '%s'", path.c_str(), norm.c_str());
        http_server_request_set_content_type(request, "application/json");
        http_server_request_send_string(request, "{\"error\":\"invalid path\"}");
        return ERROR_NONE;
    }

    std::ostringstream json;
    json << "{\"path\":\"" << norm << "\",\"entries\":[";
    struct FsIterContext {
        std::ostringstream& json;
        uint16_t count = 0;
    };
    FsIterContext fs_iter_context { json };
    // Special handling for root: show available mount points
    if (norm == "/") {
        file_system_for_each(&fs_iter_context, [] (auto* fs, void* context) {
            auto* fs_iter_context = static_cast<FsIterContext*>(context);
            char path[128];
            if (file_system_is_mounted(fs) && file_system_get_path(fs, path, sizeof(path)) == ERROR_NONE && strcmp(path, "/system") != 0) {
                fs_iter_context->count++;
                if (fs_iter_context->count != 1) fs_iter_context->json << ","; // add separator between json array entries
                fs_iter_context->json << "{\"name\":\"" << path << "\",\"type\":\"dir\",\"size\":0}";
            }
            return true;
        });

        json << "]}";
    } else {
        std::vector<dirent> entries;
        int res = file::scandir(norm, entries, file::direntFilterDotEntries, nullptr);
        if (res < 0) {
            http_server_request_set_content_type(request, "application/json");
            http_server_request_send_string(request, "{\"error\":\"scan failed\"}");
            return ERROR_NONE;
        }
        bool first = true;
        for (auto& e : entries) {
            if (!first) json << ','; else first = false;
            std::string name = e.d_name;
            bool is_dir = (e.d_type == file::TT_DT_DIR || e.d_type == file::TT_DT_CHR);
            std::string full = norm + "/" + name;
            long size = 0;
            if (!is_dir) {
                struct stat st;
                if (stat(full.c_str(), &st) == 0) {
                    size = st.st_size;
                }
            }
            json << "{\"name\":\"" << escapeJson(name) << "\",\"type\":\"" << (is_dir?"dir":"file") << "\",\"size\":" << size << "}";
        }
        json << "]}";
    }

    http_server_request_set_content_type(request, "application/json");
    http_server_request_send_string(request, json.str().c_str());
    return ERROR_NONE;
}

error_t WebServerService::handleFsDownload(HttpServerRequest* request, void*) {
    std::string path;
    if (!getQueryParam(request, "path", path) || path.empty()) {
        http_server_request_send_error(request, 400, "path required");
        return ERROR_UNDEFINED;
    }
    std::string norm = normalizePath(path);
    if (!isAllowedBasePath(norm) || !file::isFile(norm)) {
        LOG_W(TAG, "GET /fs/download - not found or invalid path: '%s' normalized: '%s'", path.c_str(), norm.c_str());
        http_server_request_send_error(request, 404, "not found");
        return ERROR_UNDEFINED;
    }
    http_server_request_set_content_type(request, getContentType(norm));
    // Suggest download - build header into a local string so it remains valid
    std::string fname = file::getLastPathSegment(norm);
    std::string disposition = std::string("attachment; filename=\"") + fname + "\"";
    // RFC5987 fallback (filename*): percent-encode UTF-8 bytes for wider browser compatibility
    auto pctEncode = [](const std::string& s)->std::string{
        std::ostringstream oss;
        for (unsigned char c : s) {
            if (std::isalnum(c) || c=='-' || c=='.' || c=='_' || c=='~') {
                oss << c;
            } else {
                oss << '%';
                std::ostringstream hex;
                hex << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)c;
                oss << hex.str();
            }
        }
        return oss.str();
    };
    std::string pct = pctEncode(fname);
    if (!pct.empty()) {
        disposition += std::string("; filename*=UTF-8''") + pct;
    }
    // Set single Content-Disposition header (avoid adding duplicate headers)
    http_server_request_set_header(request, "Content-Disposition", disposition.c_str());
    FILE* fp = fopen(norm.c_str(), "rb");
    if (!fp) { http_server_request_send_error(request, 500, "open failed"); return ERROR_UNDEFINED; }
    if (http_server_request_send_chunk_start(request) != ERROR_NONE) { fclose(fp); return ERROR_UNDEFINED; }
    char buf[512]; size_t n;
    while ((n = fread(buf,1,sizeof(buf),fp))>0) {
        if (http_server_request_send_chunk(request, buf, n) != ERROR_NONE) { fclose(fp); return ERROR_UNDEFINED; }
    }
    fclose(fp);
    http_server_request_send_chunk_end(request);
    return ERROR_NONE;
}

error_t WebServerService::handleFsUpload(HttpServerRequest* request, void*) {
    std::string path;

    // Log raw query and decoded path for diagnostics
    char qbuf[256];
    if (http_server_request_get_query(request, qbuf, sizeof(qbuf)) > 0) {
        LOG_I(TAG, "POST /fs/upload raw query: %s", qbuf);
    }

    if (!getQueryParam(request, "path", path) || path.empty()) {
        http_server_request_send_error(request, 400, "path required");
        return ERROR_UNDEFINED;
    }

    // Log decoded path and headers
    char content_type[64] = {0};
    http_server_request_get_header(request, "Content-Type", content_type, sizeof(content_type));
    std::string norm = normalizePath(path);
    uint64_t content_length = http_server_request_get_content_length(request);
    LOG_I(TAG, "POST /fs/upload decoded path: '%s' normalized: '%s' Content-Length: %d Content-Type: %s", path.c_str(), norm.c_str(), (int)content_length, content_type[0] ? content_type : "(null)");

    if (!isAllowedBasePath(norm)) {
        LOG_W(TAG, "POST /fs/upload - invalid path requested: '%s' normalized: '%s'", path.c_str(), norm.c_str());
        http_server_request_send_error(request, 403, "invalid path");
        return ERROR_UNDEFINED;
    }

    if (content_length > MAX_UPLOAD_SIZE) {
        http_server_request_send_error(request, 400, "file too large");
        return ERROR_UNDEFINED;
    }

    // Ensure parent directory exists (after size check to avoid creating dirs for rejected uploads)
    if (!file::findOrCreateParentDirectory(norm, 0755)) {
        http_server_request_send_error(request, 500, "failed to create parent directory");
        return ERROR_UNDEFINED;
    }
    FILE* fp = fopen(norm.c_str(), "wb");
    if (!fp) { http_server_request_send_error(request, 500, "open failed"); return ERROR_UNDEFINED; }
    char buf[512]; int remaining = static_cast<int>(content_length); int received=0;
    while (remaining > 0) {
        int to_read = remaining > (int)sizeof(buf) ? (int)sizeof(buf) : remaining;
        int ret = http_server_request_receive(request, buf, to_read);
        if (ret <= 0) {
            LOG_E(TAG, "Upload recv failed with error %d", ret);
            fclose(fp);
            remove(norm.c_str());  // Clean up partial file
            http_server_request_send_error(request, 500, "recv failed");
            return ERROR_UNDEFINED;
        }
        size_t written = fwrite(buf, 1, ret, fp);
        if (written != (size_t)ret) {
            fclose(fp);
            remove(norm.c_str());
            http_server_request_send_error(request, 500, "write failed");
            return ERROR_UNDEFINED;
        }
        remaining -= ret;
        received += ret;
    }
    fclose(fp);
    http_server_request_set_content_type(request, "text/plain");
    std::string msg = std::string("Uploaded ") + std::to_string(received) + " bytes";
    http_server_request_send_string(request, msg.c_str());
    return ERROR_NONE;
}

// Generic GET dispatcher for /fs/* URIs
error_t WebServerService::handleFsGenericGet(HttpServerRequest* request, void* user_ctx) {
    // Auth check for all /fs/* endpoints (file system access is sensitive)
    bool authPassed = false;
    error_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    char uri[256];
    http_server_request_get_uri(request, uri, sizeof(uri));
    if (uriMatches(uri, "/fs/list")) return handleFsList(request, user_ctx);
    if (uriMatches(uri, "/fs/download")) return handleFsDownload(request, user_ctx);
    if (uriMatches(uri, "/fs/tree")) return handleFsTree(request, user_ctx);
    LOG_W(TAG, "GET %s - not found in fs generic dispatcher", uri);
    http_server_request_send_error(request, 404, "not found");
    return ERROR_UNDEFINED;
}

// Generic POST dispatcher for /fs/* URIs
error_t WebServerService::handleFsGenericPost(HttpServerRequest* request, void* user_ctx) {
    // Auth check for all /fs/* endpoints (file system access is sensitive)
    bool authPassed = false;
    error_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    char uri[256];
    http_server_request_get_uri(request, uri, sizeof(uri));
    if (uriMatches(uri, "/fs/mkdir")) return handleFsMkdir(request, user_ctx);
    if (uriMatches(uri, "/fs/delete")) return handleFsDelete(request, user_ctx);
    if (uriMatches(uri, "/fs/rename")) return handleFsRename(request, user_ctx);
    if (uriMatches(uri, "/fs/upload")) return handleFsUpload(request, user_ctx);
    LOG_W(TAG, "POST %s - not found in fs generic dispatcher", uri);
    http_server_request_send_error(request, 404, "not found");
    return ERROR_UNDEFINED;
}

// Admin dispatcher for consolidated small POST endpoints (e.g. sync, reboot)
error_t WebServerService::handleAdminPost(HttpServerRequest* request, void* user_ctx) {
    // Auth check for all /admin/* endpoints (admin actions are sensitive)
    bool authPassed = false;
    error_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    char uri[256];
    http_server_request_get_uri(request, uri, sizeof(uri));
    if (strncmp(uri, "/admin/reboot", 13) == 0) return handleReboot(request, user_ctx);
    LOG_I(TAG, "POST %s - not found in admin dispatcher", uri);
    http_server_request_send_error(request, 404, "not found");
    return ERROR_UNDEFINED;
}

// API GET dispatcher - returns JSON system information
// Note: /api/sysinfo is intentionally public for monitoring use cases
error_t WebServerService::handleApiGet(HttpServerRequest* request, void* user_ctx) {
    char uri[256];
    http_server_request_get_uri(request, uri, sizeof(uri));

    // Public endpoint: sysinfo (basic device info for monitoring)
    if (strncmp(uri, "/api/sysinfo", 12) == 0) {
        return handleApiSysinfo(request, user_ctx);
    }

    // Protected endpoints require authentication
    bool authPassed = false;
    error_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    // Auth-protected endpoints
    if (strncmp(uri, "/api/apps", 9) == 0) {
        return handleApiApps(request, user_ctx);
    }
    if (strncmp(uri, "/api/wifi", 9) == 0) {
        return handleApiWifi(request, user_ctx);
    }
    if (strncmp(uri, "/api/screenshot", 15) == 0) {
        return handleApiScreenshot(request, user_ctx);
    }

    LOG_W(TAG, "GET %s - not found in api dispatcher", uri);
    http_server_request_send_error(request, 404, "not found");
    return ERROR_UNDEFINED;
}

// API POST dispatcher - all POST endpoints require authentication
error_t WebServerService::handleApiPost(HttpServerRequest* request, void* user_ctx) {
    bool authPassed = false;
    error_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    char uri[256];
    http_server_request_get_uri(request, uri, sizeof(uri));
    if (strncmp(uri, "/api/apps/run", 13) == 0) {
        return handleApiAppsRun(request, user_ctx);
    }
    if (strncmp(uri, "/api/apps/uninstall", 19) == 0) {
        return handleApiAppsUninstall(request, user_ctx);
    }

    LOG_W(TAG, "POST %s - not found in api dispatcher", uri);
    http_server_request_send_error(request, 404, "not found");
    return ERROR_UNDEFINED;
}

// API PUT dispatcher - all PUT endpoints require authentication
error_t WebServerService::handleApiPut(HttpServerRequest* request, void* user_ctx) {
    bool authPassed = false;
    error_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    char uri[256];
    http_server_request_get_uri(request, uri, sizeof(uri));
    if (strncmp(uri, "/api/apps/install", 17) == 0) {
        return handleApiAppsInstall(request, user_ctx);
    }

    LOG_W(TAG, "PUT %s - not found in api dispatcher", uri);
    http_server_request_send_error(request, 404, "not found");
    return ERROR_UNDEFINED;
}

error_t WebServerService::handleApiSysinfo(HttpServerRequest* request, void*) {
    LOG_I(TAG, "GET /api/sysinfo");

    std::ostringstream json;
    json << "{";

    // Firmware info
    json << "\"firmware\":{";
    json << "\"version\":\"" << TT_VERSION << "\",";
#ifdef ESP_PLATFORM
    json << "\"idf_version\":\"" << ESP_IDF_VERSION_MAJOR << "." << ESP_IDF_VERSION_MINOR << "." << ESP_IDF_VERSION_PATCH << "\"";
#else
    json << "\"idf_version\":\"n/a\"";
#endif
    json << "},";

    // Chip info
    json << "\"chip\":{";
#ifdef ESP_PLATFORM
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    json << "\"model\":\"" << getChipModelName(chip_info.model) << "\",";
    json << "\"cores\":" << (int)chip_info.cores << ",";
    json << "\"revision\":" << (int)chip_info.revision << ",";

    // Decode features into an array of strings
    json << "\"features\":[";
    bool first_feature = true;
    if (chip_info.features & CHIP_FEATURE_EMB_FLASH) {
        json << "\"Embedded Flash\"";
        first_feature = false;
    }
    if (chip_info.features & CHIP_FEATURE_WIFI_BGN) {
        if (!first_feature) json << ",";
        json << "\"WiFi 2.4GHz\"";
        first_feature = false;
    }
    if (chip_info.features & CHIP_FEATURE_BLE) {
        if (!first_feature) json << ",";
        json << "\"BLE\"";
        first_feature = false;
    }
    if (chip_info.features & CHIP_FEATURE_BT) {
        if (!first_feature) json << ",";
        json << "\"Bluetooth Classic\"";
        first_feature = false;
    }
    if (chip_info.features & CHIP_FEATURE_IEEE802154) {
        if (!first_feature) json << ",";
        json << "\"IEEE 802.15.4\"";
        first_feature = false;
    }
    if (chip_info.features & CHIP_FEATURE_EMB_PSRAM) {
        if (!first_feature) json << ",";
        json << "\"Embedded PSRAM\"";
    }
    json << "],";

    // Internal flash size
    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);
    json << "\"flash_size\":" << flash_size;
#else
    json << "\"model\":\"" << CONFIG_TT_DEVICE_ID << "\",";
    json << "\"cores\":0,";
    json << "\"revision\":0,";
    json << "\"features\":[],";
    json << "\"flash_size\":0";
#endif
    json << "},";

    // Memory - Internal heap
    json << "\"heap\":{";
#ifdef ESP_PLATFORM
    size_t heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t heap_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t heap_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    json << "\"free\":" << heap_free << ",";
    json << "\"total\":" << heap_total << ",";
    json << "\"min_free\":" << heap_min_free << ",";
    json << "\"largest_block\":" << heap_largest;
#else
    json << "\"free\":0,\"total\":0,\"min_free\":0,\"largest_block\":0";
#endif
    json << "},";

    // Memory - PSRAM (external)
    json << "\"psram\":{";
#ifdef ESP_PLATFORM
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    json << "\"free\":" << psram_free << ",";
    json << "\"total\":" << psram_total << ",";
    json << "\"min_free\":" << psram_min_free << ",";
    json << "\"largest_block\":" << psram_largest;
#else
    json << "\"free\":0,\"total\":0,\"min_free\":0,\"largest_block\":0";
#endif
    json << "},";

    // Storage info
    json << "\"storage\":{";

    struct FsIterContext {
        std::ostringstream& json;
        uint16_t count = 0;
    };
    FsIterContext fs_iter_context { json };
    file_system_for_each(&fs_iter_context, [] (auto* fs, void* context) {
        char mount_path[128] = "";
        if (file_system_get_path(fs, mount_path, sizeof(mount_path)) != ERROR_NONE) return true;
        if (strcmp(mount_path, "/system") == 0) return true; // Hide system partition

        bool mounted = file_system_is_mounted(fs);
        auto* fs_iter_context = static_cast<FsIterContext*>(context);
        auto& json_context = fs_iter_context->json;
        std::string mount_path_cpp = mount_path;

        fs_iter_context->count++;
        if (fs_iter_context->count != 1) json_context << ","; // add separator between json array entries
        json_context << "\"" << mount_path_cpp.substr(1) << "\":{";

#ifdef ESP_PLATFORM
        uint64_t storage_total = 0, storage_free = 0;
        if (esp_vfs_fat_info(mount_path, &storage_total, &storage_free) == ESP_OK) {
            json_context << "\"free\":" << storage_free << ",";
            json_context << "\"total\":" << storage_total << ",";
        } else {
            json_context << "\"free\":0,";
            json_context << "\"total\":0,";
        }
#else
        json_context << "\"free\":0,";
        json_context << "\"total\":0,";
#endif

        json_context << "\"mounted\":" << (mounted ? "true" : "false") << "";
        json_context << "}";
        return true;
    });

    json << "},";  // end storage

    // Uptime (in seconds)
    TickType_t ticks = xTaskGetTickCount();
    float uptime_sec = static_cast<float>(ticks) / configTICK_RATE_HZ;
    json << "\"uptime\":" << static_cast<int>(uptime_sec) << ",";

    // Task count
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    json << "\"task_count\":" << task_count << ",";

    // Feature flags
    json << "\"features_enabled\":{";
#if TT_FEATURE_SCREENSHOT_ENABLED
    json << "\"screenshot\":true";
#else
    json << "\"screenshot\":false";
#endif
    json << "}";

    json << "}";

    http_server_request_set_content_type(request, "application/json");
    http_server_request_send_string(request, json.str().c_str());
    return ERROR_NONE;
}

// GET /api/apps - List installed apps
error_t WebServerService::handleApiApps(HttpServerRequest* request, void*) {
    LOG_I(TAG, "GET /api/apps");

    std::vector<const ::AppManifest*> manifests;
    app_manager_for_each_manifest([](const ::AppManifest* manifest, void* context) {
        static_cast<std::vector<const ::AppManifest*>*>(context)->push_back(manifest);
    }, &manifests);

    std::ostringstream json;
    json << "{\"apps\":[";

    bool first = true;
    for (const auto* manifest : manifests) {
        if (!first) json << ",";
        first = false;

        json << "{";
        json << "\"id\":\"" << escapeJson(manifest->id) << "\",";
        json << "\"name\":\"" << escapeJson(manifest->name) << "\",";

        const char* category = "user";
        if (manifest->category == APP_CATEGORY_SYSTEM) category = "system";
        else if (manifest->category == APP_CATEGORY_SETTINGS) category = "settings";
        json << "\"category\":\"" << category << "\",";

        json << "\"isExternal\":" << (manifest->location.type == APP_LOCATION_PATH ? "true" : "false") << ",";
        json << "\"hidden\":" << ((manifest->flags & APP_MANIFEST_FLAG_HIDDEN) ? "true" : "false");

        json << "}";
    }

    json << "]}";

    http_server_request_set_content_type(request, "application/json");
    http_server_request_send_string(request, json.str().c_str());
    return ERROR_NONE;
}

// POST /api/apps/run?id=xxx - Run an app
error_t WebServerService::handleApiAppsRun(HttpServerRequest* request, void*) {
    LOG_I(TAG, "POST /api/apps/run");

    std::string appId;
    if (!getQueryParam(request, "id", appId) || appId.empty()) {
        http_server_request_send_error(request, 400, "id parameter required");
        return ERROR_UNDEFINED;
    }

    AppManifest manifest;
    if (app_manager_find_manifest(appId.c_str(), &manifest) != ERROR_NONE) {
        http_server_request_send_error(request, 404, "app not found");
        return ERROR_UNDEFINED;
    }

    // Every app instance gets its own task now, so there's no "stop the existing one first" -
    // this just starts a fresh instance alongside whatever's already running.
    AppInstanceId instance_id = 0;
    app_start(appId.c_str(), 0, nullptr, &instance_id);

    LOG_I(TAG, "[200] /api/apps/run %s", appId.c_str());
    http_server_request_send_string(request, "ok");
    return ERROR_NONE;
}

// POST /api/apps/uninstall?id=xxx - Uninstall an app
error_t WebServerService::handleApiAppsUninstall(HttpServerRequest* request, void*) {
    LOG_I(TAG, "POST /api/apps/uninstall");

    std::string appId;
    if (!getQueryParam(request, "id", appId) || appId.empty()) {
        http_server_request_send_error(request, 400, "id parameter required");
        return ERROR_UNDEFINED;
    }

    AppManifest manifest;
    if (app_manager_find_manifest(appId.c_str(), &manifest) != ERROR_NONE) {
        LOG_I(TAG, "[200] /api/apps/uninstall %s (app wasn't installed)", appId.c_str());
        http_server_request_send_string(request, "ok");
        return ERROR_NONE;
    }

    // Only allow uninstalling external (side-loaded) apps
    if (manifest.location.type != APP_LOCATION_PATH) {
        http_server_request_send_error(request, 403, "cannot uninstall system apps");
        return ERROR_UNDEFINED;
    }

    if (app_uninstall(appId.c_str()) == ERROR_NONE) {
        LOG_I(TAG, "[200] /api/apps/uninstall %s", appId.c_str());
        http_server_request_send_string(request, "ok");
        return ERROR_NONE;
    } else {
        LOG_W(TAG, "[500] /api/apps/uninstall %s", appId.c_str());
        http_server_request_send_error(request, 500, "uninstall failed");
        return ERROR_UNDEFINED;
    }
}

// PUT /api/apps/install - Install an app from multipart form upload
error_t WebServerService::handleApiAppsInstall(HttpServerRequest* request, void*) {
    LOG_I(TAG, "PUT /api/apps/install");

    std::string boundary;
    if (!network::getMultiPartBoundaryOrSendError(request, boundary)) {
        return ERROR_UNDEFINED;
    }

    size_t content_left = http_server_request_get_content_length(request);
    constexpr size_t MAX_APP_UPLOAD_SIZE = 20 * 1024 * 1024;

    // Read headers until empty line (skip boundary line first)
    auto content_headers_data = network::receiveTextUntil(request, "\r\n\r\n");
    content_left -= content_headers_data.length();

    // Split headers into lines and filter empty ones
    auto content_header_lines = string::split(content_headers_data, "\r\n");
    std::vector<std::string> content_headers;
    for (auto& line : content_header_lines) {
        if (!line.empty()) {
            content_headers.push_back(line);
        }
    }

    auto content_disposition_map = network::parseContentDisposition(content_headers);
    if (content_disposition_map.empty()) {
        LOG_W(TAG, "parseContentDisposition returned empty map for: %s", content_headers_data.c_str());
        http_server_request_send_error(request, 400, "invalid content disposition");
        return ERROR_UNDEFINED;
    }

    auto filename_entry = content_disposition_map.find("filename");
    if (filename_entry == content_disposition_map.end()) {
        LOG_W(TAG, "filename not found in content disposition map");
        http_server_request_send_error(request, 400, "filename parameter missing");
        return ERROR_UNDEFINED;
    }

    // Calculate file size
    auto boundary_and_newlines_after_file = std::format("\r\n--{}--\r\n", boundary);
    if (content_left <= boundary_and_newlines_after_file.length()) {
        http_server_request_send_error(request, 400, "invalid multipart payload");
        return ERROR_UNDEFINED;
    }

    auto file_size = content_left - boundary_and_newlines_after_file.length();
    if (file_size == 0 || file_size > MAX_APP_UPLOAD_SIZE) {
        http_server_request_send_error(request, 400, "file too large");
        return ERROR_UNDEFINED;
    }

    // Create tmp directory
    const std::string tmp_path = getTempPath();
    if (!file::findOrCreateDirectory(tmp_path, 0777)) {
        http_server_request_send_error(request, 500, "failed to create temp directory");
        return ERROR_UNDEFINED;
    }

    std::string safe_name = file::getLastPathSegment(filename_entry->second);
    if (safe_name.empty() || safe_name.find("..") != std::string::npos ||
        safe_name.find('/') != std::string::npos || safe_name.find('\\') != std::string::npos) {
        http_server_request_send_error(request, 400, "invalid filename");
        return ERROR_UNDEFINED;
    }
    auto file_path = std::format("{}/{}", tmp_path, safe_name);

    if (network::receiveFile(request, file_size, file_path) != file_size) {
        file::deleteFile(file_path);
        http_server_request_send_error(request, 500, "failed to save file");
        return ERROR_UNDEFINED;
    }

    content_left -= file_size;

    // Read and discard trailing boundary
    if (!network::readAndDiscardOrSendError(request, boundary_and_newlines_after_file)) {
        return ERROR_UNDEFINED;
    }

    // Install the app
    if (app_install(file_path.c_str()) != ERROR_NONE) {
        file::deleteFile(file_path);
        http_server_request_send_error(request, 500, "installation failed");
        return ERROR_UNDEFINED;
    }

    // Cleanup temp file
    if (!file::deleteFile(file_path)) {
        LOG_W(TAG, "Failed to delete temp file %s", file_path.c_str());
    }

    LOG_I(TAG, "[200] /api/apps/install -> %s", file_path.c_str());
    http_server_request_send_string(request, "ok");
    return ERROR_NONE;
}

// Helper to convert radio state to string
static const char* radioStateToJsonString(wifi::RadioState state) {
    switch (state) {
        case wifi::RadioState::On: return "on";
        case wifi::RadioState::OnPending: return "turning_on";
        case wifi::RadioState::Off: return "off";
        case wifi::RadioState::OffPending: return "turning_off";
        case wifi::RadioState::ConnectionPending: return "connecting";
        case wifi::RadioState::ConnectionActive: return "connected";
        default: return "unknown";
    }
}

// GET /api/wifi - WiFi status
error_t WebServerService::handleApiWifi(HttpServerRequest* request, void*) {
    LOG_I(TAG, "GET /api/wifi");

    auto state = wifi::getRadioState();
    auto ip = wifi::getIp();
    auto ssid = wifi::getConnectionTarget();
    auto rssi = wifi::getRssi();
    bool secure = wifi::isConnectionSecure();

    std::ostringstream json;
    json << "{";
    json << "\"state\":\"" << radioStateToJsonString(state) << "\",";
    json << "\"ip\":\"" << escapeJson(ip) << "\",";
    json << "\"ssid\":\"" << escapeJson(ssid) << "\",";
    json << "\"rssi\":" << rssi << ",";
    json << "\"secure\":" << (secure ? "true" : "false");
    json << "}";

    http_server_request_set_content_type(request, "application/json");
    http_server_request_send_string(request, json.str().c_str());
    return ERROR_NONE;
}

// GET /api/screenshot - Capture and return screenshot as PNG
// Screenshots are saved to SD card root (if available) or /data with incrementing numbers
error_t WebServerService::handleApiScreenshot(HttpServerRequest* request, void*) {
    LOG_I(TAG, "GET /api/screenshot");

#if TT_FEATURE_SCREENSHOT_ENABLED
    // Determine save location: prefer SD card root if mounted, otherwise /data
    std::string save_path = getDataPath();

    // Find next available filename with incrementing number
    std::string screenshot_path;
    bool found_slot = false;
    for (int i = 1; i <= 9999; ++i) {
        screenshot_path = std::format("{}/webscreenshot{}.png", save_path, i);
        if (!file::isFile(screenshot_path)) {
            found_slot = true;
            break;
        }
    }
    if (!found_slot) {
        http_server_request_send_error(request, 500, "no available screenshot slots");
        return ERROR_UNDEFINED;
    }

    LOG_I(TAG, "Screenshot will be saved to: %s", screenshot_path.c_str());

    // LVGL's lodepng uses lv_fs which requires the "A:" prefix
    std::string lvgl_screenshot_path = lvgl::PATH_PREFIX + screenshot_path;

    // Capture screenshot using LVGL
    if (lvgl_try_lock(pdMS_TO_TICKS(100))) {
        bool success = lv_screenshot_create(lv_scr_act(), LV_100ASK_SCREENSHOT_SV_PNG, lvgl_screenshot_path.c_str());
        lvgl_unlock();

        if (!success) {
            LOG_E(TAG, "lv_screenshot_create failed for path: %s", lvgl_screenshot_path.c_str());
            http_server_request_send_error(request, 500, "screenshot capture failed");
            return ERROR_UNDEFINED;
        }
        LOG_I(TAG, "Screenshot captured successfully");
    } else {
        LOG_E(TAG, "Could not acquire LVGL lock within 100ms");
        http_server_request_send_error(request, 500, "could not acquire LVGL lock");
        return ERROR_UNDEFINED;
    }

    // Send the file (use regular path for fopen, not LVGL path)
    http_server_request_set_content_type(request, "image/png");

    FILE* fp = fopen(screenshot_path.c_str(), "rb");
    if (!fp) {
        http_server_request_send_error(request, 500, "failed to open screenshot");
        return ERROR_UNDEFINED;
    }

    if (http_server_request_send_chunk_start(request) != ERROR_NONE) {
        fclose(fp);
        return ERROR_UNDEFINED;
    }
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (http_server_request_send_chunk(request, buf, n) != ERROR_NONE) {
            fclose(fp);
            return ERROR_UNDEFINED;
        }
    }
    fclose(fp);
    http_server_request_send_chunk_end(request);

    // File is kept on storage (not deleted) for user access
    LOG_I(TAG, "[200] /api/screenshot -> %s", screenshot_path.c_str());
    return ERROR_NONE;
#else
    http_server_request_send_error(request, 501, "screenshot feature not enabled");
    return ERROR_UNDEFINED;
#endif
}

error_t WebServerService::handleFsTree(HttpServerRequest* request, void*) {

    LOG_I(TAG, "GET /fs/tree");

    std::ostringstream json;
    json << "{";
    // Gather mount points
    auto mounts = file::getFileSystemDirents();
    json << "\"mounts\": [";
    bool firstMount = true;
    for (auto& m : mounts) {
        if (!firstMount) json << ','; else firstMount = false;
        std::string name = m.d_name;
        std::string path = (name == std::string("data") || name == std::string("/data")) ? std::string("/data") : std::string("/") + name;
        // normalize possible duplicate slash
        if (!path.starts_with("/")) path = std::string("/") + path;
        json << "{\"name\":\"" << escapeJson(name) << "\",\"path\":\"" << escapeJson(path) << "\",\"entries\": [";

        std::vector<dirent> entries;
        int res = file::scandir(path, entries, file::direntFilterDotEntries, nullptr);
        if (res > 0) {
            bool first = true;
            for (auto& e : entries) {
                if (!first) json << ','; else first = false;
                std::string en = e.d_name;
                bool is_dir = (e.d_type == file::TT_DT_DIR || e.d_type == file::TT_DT_CHR);
                json << "{\"name\":\"" << escapeJson(en) << "\",\"type\":\"" << (is_dir?"dir":"file") << "\"}";
            }
        }

        json << "]}";
    }
    json << "]}";

    http_server_request_set_content_type(request, "application/json");
    http_server_request_send_string(request, json.str().c_str());
    return ERROR_NONE;
}

// Create a directory at the specified path (POST /fs/mkdir?path=/data/newdir)
error_t WebServerService::handleFsMkdir(HttpServerRequest* request, void*) {
    std::string path;
    if (!getQueryParam(request, "path", path) || path.empty()) {
        http_server_request_send_error(request, 400, "path required");
        return ERROR_UNDEFINED;
    }
    std::string norm = normalizePath(path);
    LOG_I(TAG, "POST /fs/mkdir requested: '%s' normalized: '%s'", path.c_str(), norm.c_str());
    if (!isAllowedBasePath(norm)) {
        http_server_request_send_error(request, 403, "invalid path");
        return ERROR_UNDEFINED;
    }
    bool ok = file::findOrCreateDirectory(norm, 0755);
    if (!ok) { http_server_request_send_error(request, 500, "mkdir failed"); return ERROR_UNDEFINED; }
    http_server_request_send_string(request, "ok");
    return ERROR_NONE;
}

static bool isRootMountPoint(const std::string& path) {
    return path == "/data" || path == "/sdcard";
}

// Delete a file or directory (POST /fs/delete?path=/data/foo)
error_t WebServerService::handleFsDelete(HttpServerRequest* request, void*) {
    std::string path;
    if (!getQueryParam(request, "path", path) || path.empty()) {
        http_server_request_send_error(request, 400, "path required");
        return ERROR_UNDEFINED;
    }
    std::string norm = normalizePath(path);
    LOG_I(TAG, "POST /fs/delete requested: '%s' normalized: '%s'", path.c_str(), norm.c_str());
    if (!isAllowedBasePath(norm)) {
        http_server_request_send_error(request, 403, "invalid path");
        return ERROR_UNDEFINED;
    }
    if (isRootMountPoint(norm)) {
        http_server_request_send_error(request, 403, "cannot delete mount point");
        return ERROR_UNDEFINED;
    }
    bool ok = true;
    if (file::isDirectory(norm)) ok = file::deleteRecursively(norm);
    else if (file::isFile(norm)) ok = file::deleteFile(norm);
    else ok = false;
    if (!ok) { http_server_request_send_error(request, 500, "delete failed"); return ERROR_UNDEFINED; }
    http_server_request_send_string(request, "ok");
    return ERROR_NONE;
}

// Rename a file or folder (POST /fs/rename?path=/data/oldname&newName=newname)
error_t WebServerService::handleFsRename(HttpServerRequest* request, void*) {
    std::string path;
    std::string newName;
    if (!getQueryParam(request, "path", path) || path.empty()) {
        http_server_request_send_error(request, 400, "path required");
        return ERROR_UNDEFINED;
    }
    if (!getQueryParam(request, "newName", newName) || newName.empty()) {
        http_server_request_send_error(request, 400, "newName required");
        return ERROR_UNDEFINED;
    }
    std::string norm = normalizePath(path);
    LOG_I(TAG, "POST /fs/rename requested: '%s' normalized: '%s' -> newName: '%s'", path.c_str(), norm.c_str(), newName.c_str());
    if (!isAllowedBasePath(norm)) {
        http_server_request_send_error(request, 403, "invalid path");
        return ERROR_UNDEFINED;
    }

    // Basic validation of newName: must not contain path separators or '..'
    // Trim whitespace from newName
    auto trim = [](std::string& s){ size_t st=0; while (st<s.size() && isspace((unsigned char)s[st])) ++st; size_t ed=s.size(); while (ed>st && isspace((unsigned char)s[ed-1])) --ed; s = s.substr(st, ed-st); };
    trim(newName);
    if (newName.empty() || newName.find('/') != std::string::npos || newName.find('\\') != std::string::npos || newName.find("..") != std::string::npos) {
        http_server_request_send_error(request, 400, "invalid newName");
        return ERROR_UNDEFINED;
    }

    // compute parent directory
    std::string parent = "/";
    size_t pos = norm.find_last_of('/');
    if (pos != std::string::npos) {
        parent = (pos == 0) ? std::string("/") : norm.substr(0, pos);
    }

    if (!isAllowedBasePath(parent)) {
        http_server_request_send_error(request, 403, "invalid target parent");
        return ERROR_UNDEFINED;
    }

    std::string target = file::getChildPath(parent, newName);

    // Prevent overwrite: fail if target exists
    if (file::isFile(target) || file::isDirectory(target)) {
        http_server_request_send_error(request, 400, "target exists");
        return ERROR_UNDEFINED;
    }

    // perform rename
    int r = rename(norm.c_str(), target.c_str());
    if (r != 0) {
        int e = errno;
        LOG_W(TAG, "rename failed errno=%d (%s) -> %s -> %s", e, strerror(e), norm.c_str(), target.c_str());
        // Return errno string to client to aid debugging
        std::string msg = std::string("rename failed: ") + strerror(e);
        http_server_request_send_error(request, 500, msg.c_str());
        return ERROR_UNDEFINED;
    }
    http_server_request_send_string(request, "ok");
    return ERROR_NONE;
}

// endregion

error_t WebServerService::handleReboot(HttpServerRequest* request, void*) {

    LOG_I(TAG, "POST /reboot");
    http_server_request_send_string(request, "Rebooting...");

    // Reboot after a short delay to allow response to be sent
    vTaskDelay(pdMS_TO_TICKS(2000));
#ifdef ESP_PLATFORM
    esp_restart();
#else
    LOG_W(TAG, "Reboot is not supported on this platform");
#endif

    return ERROR_NONE; // Unreachable on ESP_PLATFORM, but satisfies function signature
}

error_t WebServerService::handleAssets(HttpServerRequest* request, void*) {
    // Auth check for UI access control
    bool authPassed = false;
    error_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    char uri[256];
    http_server_request_get_uri(request, uri, sizeof(uri));
    LOG_I(TAG, "GET %s", uri);

    // Special case: serve favicon from system assets
    if (strcmp(uri, "/favicon.ico") == 0) {
        std::string faviconPathStr = std::string(file::MOUNT_POINT_SYSTEM) + "/spinner.png";
        const char* faviconPath = faviconPathStr.c_str();
        if (file::isFile(faviconPath)) {
            http_server_request_set_content_type(request, "image/png");
            http_server_request_set_header(request, "Cache-Control", "public, max-age=86400");

            FILE* fp = fopen(faviconPath, "rb");
            if (fp) {
                if (http_server_request_send_chunk_start(request) != ERROR_NONE) {
                    fclose(fp);
                    return ERROR_UNDEFINED;
                }
                char buffer[512];
                size_t bytesRead;
                while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
                    if (http_server_request_send_chunk(request, buffer, bytesRead) != ERROR_NONE) {
                        fclose(fp);
                        return ERROR_UNDEFINED;
                    }
                }
                fclose(fp);
                http_server_request_send_chunk_end(request);
                LOG_I(TAG, "[200] %s (favicon)", uri);
                return ERROR_NONE;
            }
        }
        // If favicon not found, return 404 silently (browsers handle this gracefully)
        http_server_request_send_error(request, 404, "Not found");
        return ERROR_UNDEFINED;
    }

    // Special case: if requesting dashboard.html but it doesn't exist, serve default.html
    std::string requestedPath = normalizePath(uri);
    if (requestedPath == "/.." || requestedPath.ends_with("/..") || requestedPath.find("/../") != std::string::npos) {
        http_server_request_send_error(request, 400, "invalid path");
        return ERROR_UNDEFINED;
    }

    std::string dataPath = std::string(file::MOUNT_POINT_SYSTEM) + "/app/WebServer" + requestedPath;

    if (requestedPath == "/dashboard.html" && !file::isFile(dataPath.c_str())) {
        LOG_I(TAG, "dashboard.html not found, serving default.html");
    }

    // Try to serve from Data partition first
    if (file::isFile(dataPath.c_str())) {
        http_server_request_set_content_type(request, getContentType(dataPath));

        FILE* fp = fopen(dataPath.c_str(), "rb");
        if (fp) {
            if (http_server_request_send_chunk_start(request) != ERROR_NONE) {
                fclose(fp);
                return ERROR_UNDEFINED;
            }
            char buffer[512];
            size_t bytesRead;
            while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
                if (http_server_request_send_chunk(request, buffer, bytesRead) != ERROR_NONE) {
                    fclose(fp);
                    return ERROR_UNDEFINED;
                }
            }
            fclose(fp);

            http_server_request_send_chunk_end(request);  // End of chunks
            LOG_I(TAG, "[200] %s (from Data)", uri);
            return ERROR_NONE;
        }
    }

    // Fallback to SD card
    std::string sdPath = std::string("/sdcard/tactility/webserver") + requestedPath;
    if (file::isFile(sdPath.c_str())) {
        http_server_request_set_content_type(request, getContentType(sdPath));

        FILE* fp = fopen(sdPath.c_str(), "rb");
        if (fp) {
            if (http_server_request_send_chunk_start(request) != ERROR_NONE) {
                fclose(fp);
                return ERROR_UNDEFINED;
            }
            char buffer[512];
            size_t bytesRead;
            while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
                if (http_server_request_send_chunk(request, buffer, bytesRead) != ERROR_NONE) {
                    fclose(fp);
                    return ERROR_UNDEFINED;
                }
            }
            fclose(fp);

            http_server_request_send_chunk_end(request);  // End of chunks
            LOG_I(TAG, "[200] %s (from SD)", uri);
            return ERROR_NONE;
        }
    }

    // File not found
    LOG_W(TAG, "[404] %s", uri);
    http_server_request_send_error(request, 404, "File not found");
    return ERROR_UNDEFINED;
}

extern const ServiceManifest manifest = {
    .id = "tactility.webserver",
    .createService = create<WebServerService>
};

void setWebServerEnabled(bool enabled) {
    WebServerService* instance = g_webServerInstance.load();
    if (instance != nullptr) {
        instance->setEnabled(enabled);
        // Don't log here - startServer()/stopServer() already log the actual result
    } else {
        LOG_W(TAG, "WebServer service not available, cannot %s", enabled ? "start" : "stop");
    }
}

bool isWebServerEnabled() {
    WebServerService* instance = g_webServerInstance.load();
    return instance != nullptr && instance->isEnabled();
}

} // namespace
