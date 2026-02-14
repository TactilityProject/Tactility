#ifdef ESP_PLATFORM

#include <tactility/check.h>
#include <Tactility/service/webserver/WebServerService.h>
#include <Tactility/service/webserver/AssetVersion.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/settings/WebServerSettings.h>
#include <Tactility/MountPoints.h>
#include <Tactility/file/File.h>
#include <Tactility/Logger.h>
#include <Tactility/lvgl/Statusbar.h>
#include <Tactility/Mutex.h>

#include <Tactility/TactilityConfig.h>
#include <tactility/hal/Device.h>
#include <Tactility/app/AppRegistration.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/App.h>
#include <Tactility/hal/sdcard/SdCardDevice.h>
#include <Tactility/service/wifi/Wifi.h>
#include <Tactility/network/HttpdReq.h>
#include <Tactility/network/Url.h>
#include <Tactility/Paths.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/StringUtils.h>

#include <ranges>

#if TT_FEATURE_SCREENSHOT_ENABLED
#include <lv_screenshot.h>
#endif

#include <tactility/lvgl_symbols_statusbar.h>

#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_vfs_fat.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <iomanip>
#include <lwip/ip4_addr.h>
#include <mbedtls/base64.h>
#include <sstream>

namespace tt::service::webserver {

static const auto LOGGER = tt::Logger("WebServerService");

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
#ifdef CHIP_ESP32P4
        case CHIP_ESP32P4: return "ESP32-P4";
#endif
#ifdef CHIP_ESP32C5
        case CHIP_ESP32C5: return "ESP32-C5";
#endif
#ifdef CHIP_ESP32C61
        case CHIP_ESP32C61: return "ESP32-C61";
#endif
        default:           return "Unknown";
    }
}

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
static esp_err_t sendUnauthorized(httpd_req_t* request, const char* message) {
    httpd_resp_set_hdr(request, "WWW-Authenticate", "Basic realm=\"Tactility\"");
    httpd_resp_send_err(request, HTTPD_401_UNAUTHORIZED, message);
    return ESP_OK;  // Response was sent successfully
}

// Helper to validate HTTP Basic Auth on sensitive endpoints
// Returns ESP_OK with authPassed=true if auth succeeded or is disabled
// Returns ESP_OK with authPassed=false if auth failed (401 response already sent)
static esp_err_t validateRequestAuth(httpd_req_t* request, bool& authPassed) {
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
        return ESP_OK;  // Auth disabled, allow request
    }

    // Get Authorization header
    size_t auth_len = httpd_req_get_hdr_value_len(request, "Authorization");
    if (auth_len == 0) {
        return sendUnauthorized(request, "Authorization required");
    }

    std::string auth_header(auth_len + 1, '\0');
    if (httpd_req_get_hdr_value_str(request, "Authorization", auth_header.data(), auth_len + 1) != ESP_OK) {
        LOGGER.warn("Failed to read Authorization header");
        return sendUnauthorized(request, "Authorization required");
    }
    auth_header.resize(auth_len);  // Remove null terminator from string length

    // Check for "Basic " prefix
    if (auth_header.rfind("Basic ", 0) != 0) {
        LOGGER.warn("Authorization header is not Basic auth");
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
        LOGGER.warn("Failed to decode base64 credentials");
        return sendUnauthorized(request, "Invalid credentials format");
    }
    decoded.resize(actual_len);

    // Parse username:password
    size_t colon_pos = decoded.find(':');
    if (colon_pos == std::string::npos) {
        LOGGER.warn("Invalid credentials format (no colon separator)");
        return sendUnauthorized(request, "Invalid credentials format");
    }

    std::string username = decoded.substr(0, colon_pos);
    std::string password = decoded.substr(colon_pos + 1);

    // Validate against cached settings
    bool usernameMatch = secureCompare(username, settings.webServerUsername);
    bool passwordMatch = secureCompare(password, settings.webServerPassword);
    if (!usernameMatch || !passwordMatch) {
        LOGGER.warn("Invalid credentials for user '{}'", username);
        return sendUnauthorized(request, "Invalid credentials");
    }

    authPassed = true;
    return ESP_OK;  // Auth successful
}

bool WebServerService::onStart(ServiceContext& service) {
    LOGGER.info("Starting WebServer service...");

    // Register global instance
    g_webServerInstance.store(this);

    // Create statusbar icon (hidden initially, shown when server actually starts)
    statusbarIconId = lvgl::statusbar_icon_add();
    lvgl::statusbar_icon_set_visibility(statusbarIconId, false);

    // Run asset synchronization on startup
    if (!syncAssets()) {
        LOGGER.warn("Asset sync failed, but continuing with available assets");
    }

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
        LOGGER.info("WebServer enabled in settings, starting HTTP server...");
        setEnabled(true);
    } else {
        LOGGER.info("WebServer disabled in settings, NOT starting HTTP server (saves ~10KB RAM)");
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
        if (!httpServer || !httpServer->isStarted()) {
            startServer();
        }
    } else {
        if (httpServer && httpServer->isStarted()) {
            stopServer();
        }
    }
}

bool WebServerService::isEnabled() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return httpServer && httpServer->isStarted();
}

// region AP Mode WiFi Management

bool WebServerService::startApMode() {
    // Copy settings locally
    settings::webserver::WebServerSettings settings;
    {
        auto lock = g_settingsMutex.asScopedLock();
        lock.lock();
        settings = g_cachedSettings;
    }

    if (settings.wifiMode != settings::webserver::WiFiMode::AccessPoint) {
        LOGGER.info("Not in AP mode, skipping AP WiFi initialization");
        return true;  // Not an error, just not needed
    }

    LOGGER.info("Starting WiFi in Access Point mode...");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        LOGGER.error("esp_wifi_init() failed");
        return false;
    }
    apWifiInitialized = true;

    // Create the AP network interface
    apNetif = esp_netif_create_default_wifi_ap();
    if (apNetif == nullptr) {
        LOGGER.error("esp_netif_create_default_wifi_ap() failed");
        esp_wifi_deinit();
        apWifiInitialized = false;
        return false;
    }

    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) {
        LOGGER.error("esp_wifi_set_mode(AP) failed");
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
        LOGGER.error("esp_netif_dhcps_stop() failed");
        stopApMode();
        return false;
    }

    if (esp_netif_set_ip_info(apNetif, &ip_info) != ESP_OK) {
        LOGGER.error("esp_netif_set_ip_info() failed");
        stopApMode();
        return false;
    }

    if (esp_netif_dhcps_start(apNetif) != ESP_OK) {
        LOGGER.error("esp_netif_dhcps_start() failed");
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
        LOGGER.info("AP configured with OPEN authentication (user choice)");
    } else if (settings.apPassword.length() >= 8 && settings.apPassword.length() <= 63) {
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        strncpy(reinterpret_cast<char*>(wifi_config.ap.password), settings.apPassword.c_str(), sizeof(wifi_config.ap.password) - 1);
        wifi_config.ap.password[sizeof(wifi_config.ap.password) - 1] = '\0';
        LOGGER.info("AP configured with WPA2-PSK authentication");
    } else {
        if (!settings.apPassword.empty()) {
            LOGGER.warn("AP password invalid (must be 8-63 chars, got {}) - using OPEN mode", settings.apPassword.length());
        }
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        LOGGER.warn("AP configured with OPEN authentication (no password)");
    }

    wifi_config.ap.max_connection = 4;
    wifi_config.ap.channel = settings.apChannel;

    if (esp_wifi_set_config(WIFI_IF_AP, &wifi_config) != ESP_OK) {
        LOGGER.error("esp_wifi_set_config(AP) failed");
        stopApMode();
        return false;
    }

    if (esp_wifi_start() != ESP_OK) {
        LOGGER.error("esp_wifi_start() failed");
        stopApMode();
        return false;
    }

    LOGGER.info("WiFi AP started - SSID: '{}', Channel: {}, IP: 192.168.4.1", settings.apSsid, settings.apChannel);
    return true;
}

void WebServerService::stopApMode() {
    if (apWifiInitialized) {
        esp_err_t err;
        err = esp_wifi_stop();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
            LOGGER.warn("esp_wifi_stop() in cleanup: {}", esp_err_to_name(err));
        }
        LOGGER.info("WiFi AP stopped");

        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK) {
            LOGGER.warn("esp_wifi_set_mode() in cleanup: {}", esp_err_to_name(err));
        }
        LOGGER.info("Wifi mode set back to STA");

        apWifiInitialized = false;
    }

    if (apNetif != nullptr) {
        esp_netif_destroy(apNetif);
        apNetif = nullptr;
    }
}

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
            LOGGER.error("Failed to start AP mode WiFi - HTTP server will not start");
            return false;
        }
    }

    // NOTE: If you see 'no slots left for registering handler', increase CONFIG_HTTPD_MAX_URI_HANDLERS in sdkconfig (default is 8, 16+ recommended for many endpoints)
    void* ctx = this;  // Avoid IDE warnings about 'this' in designated initializers
    std::vector<httpd_uri_t> handlers = {
        {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = handleRoot,
            .user_ctx  = ctx
        },
        // Note: /upload removed in favor of POST /fs/upload handled by /fs/* dispatcher
        {
            .uri       = "/filebrowser",
            .method    = HTTP_GET,
            .handler   = handleFileBrowser,
            .user_ctx  = ctx
        },
        // Consolidated /fs/* handlers (dispatch internally) to save uri handler slots
        {
            .uri       = "/fs/*",
            .method    = HTTP_GET,
            .handler   = handleFsGenericGet,
            .user_ctx  = ctx
        },
        {
            .uri       = "/fs/*",
            .method    = HTTP_POST,
            .handler   = handleFsGenericPost,
            .user_ctx  = ctx
        },
        // Consolidated admin POST endpoints to save handler slots
        {
            .uri       = "/admin/*",
            .method    = HTTP_POST,
            .handler   = handleAdminPost,
            .user_ctx  = ctx
        },
        // API endpoints for system info, apps, wifi, etc
        {
            .uri       = "/api/*",
            .method    = HTTP_GET,
            .handler   = handleApiGet,
            .user_ctx  = ctx
        },
        {
            .uri       = "/api/*",
            .method    = HTTP_POST,
            .handler   = handleApiPost,
            .user_ctx  = ctx
        },
        {
            .uri       = "/api/*",
            .method    = HTTP_PUT,
            .handler   = handleApiPut,
            .user_ctx  = ctx
        },
        {
            .uri       = "/*",  // Catch-all for dynamic assets
            .method    = HTTP_GET,
            .handler   = handleAssets,
            .user_ctx  = ctx
        }
    };
    
    httpServer = std::make_unique<network::HttpServer>(
        settings.webServerPort,
        "0.0.0.0",
        handlers,
        8192  // Stack size
    );
    
    httpServer->start();
    if (!httpServer->isStarted()) {
        LOGGER.error("Failed to start HTTP server on port {}", settings.webServerPort);
        httpServer.reset();
        return false;
    }

    LOGGER.info("HTTP server started successfully on port {}", settings.webServerPort);
    publish_event(this, WebServerEvent::WebServerStarted);

    // Show statusbar icon
    if (statusbarIconId >= 0) {
        lvgl::statusbar_icon_set_image(statusbarIconId, LVGL_SYMBOL_CLOUD);
        lvgl::statusbar_icon_set_visibility(statusbarIconId, true);
        LOGGER.info("WebServer statusbar icon shown ({} mode)",
                 settings.wifiMode == settings::webserver::WiFiMode::AccessPoint ? "AP" : "Station");
    }

    return true;
}

void WebServerService::stopServer() {
    if (!httpServer) {
        return;
    }

    httpServer->stop();
    httpServer.reset();

    // Stop AP mode WiFi if we started it
    if (apWifiInitialized || apNetif != nullptr) {
        stopApMode();
    }

    LOGGER.info("HTTP server stopped");
    publish_event(this, WebServerEvent::WebServerStopped);

    if (statusbarIconId >= 0) {
        lvgl::statusbar_icon_set_visibility(statusbarIconId, false);
    }
}

// region Endpoints



esp_err_t WebServerService::handleRoot(httpd_req_t* request) {
    LOGGER.info("GET / -> redirecting to /dashboard.html");
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "/dashboard.html");
    return httpd_resp_send(request, nullptr, 0);
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
    return path == "/data" || path.starts_with("/data/") || path == "/sdcard" || path.starts_with("/sdcard/");
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

static bool getQueryParam(httpd_req_t* req, const char* key, std::string& out) {
    size_t len = httpd_req_get_url_query_len(req) + 1;
    if (len <= 1) return false;
    std::unique_ptr<char[]> buf(new char[len]);
    if (httpd_req_get_url_query_str(req, buf.get(), len) != ESP_OK) return false;
    // Allocate buffer large enough for the entire query string (worst case)
    std::unique_ptr<char[]> value(new char[len]);
    if (httpd_query_key_value(buf.get(), key, value.get(), len) == ESP_OK) {
        out = value.get();
        return true;
    }
    return false;
}

static bool uriMatches(const char* uri, const char* route) {
    const size_t n = strlen(route);
    return strncmp(uri, route, n) == 0 && (uri[n] == '\0' || uri[n] == '?' || uri[n] == '/');
}

esp_err_t WebServerService::handleFileBrowser(httpd_req_t* request) {
    LOGGER.info("GET /filebrowser -> redirecting to /dashboard.html#files");
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "/dashboard.html#files");
    return httpd_resp_send(request, nullptr, 0);
}

esp_err_t WebServerService::handleFsList(httpd_req_t* request) {
    std::string path;
    // Log raw query string for diagnostics
    size_t qlen = httpd_req_get_url_query_len(request) + 1;
    if (qlen > 1) {
        std::unique_ptr<char[]> qbuf(new char[qlen]);
        if (httpd_req_get_url_query_str(request, qbuf.get(), qlen) == ESP_OK) {
            LOGGER.info("GET /fs/list raw query: {}", qbuf.get());
        }
    }

    if (!getQueryParam(request, "path", path) || path.empty()) path = "/";
    std::string norm = normalizePath(path);
    LOGGER.info("GET /fs/list decoded path: '{}' normalized: '{}'", path, norm);

    // Allow root path for listing mount points
    if (!isAllowedBasePath(norm, true)) {
        LOGGER.warn("GET /fs/list - invalid path requested: '{}' normalized: '{}'", path, norm);
        httpd_resp_set_type(request, "application/json");
        httpd_resp_sendstr(request, "{\"error\":\"invalid path\"}");
        return ESP_OK;
    }

    std::ostringstream json;
    json << "{\"path\":\"" << norm << "\",\"entries\":[";

    // Special handling for root: show available mount points
    if (norm == "/") {
        // Always show /data
        json << "{\"name\":\"data\",\"type\":\"dir\",\"size\":0}";

        // Show /sdcard if mounted
        const auto sdcard_devices = hal::findDevices<hal::sdcard::SdCardDevice>(hal::Device::Type::SdCard);
        for (const auto& sdcard : sdcard_devices) {
            if (sdcard->isMounted()) {
                json << ",{\"name\":\"sdcard\",\"type\":\"dir\",\"size\":0}";
                break;
            }
        }
        json << "]}";
    } else {
        std::vector<dirent> entries;
        int res = file::scandir(norm, entries, file::direntFilterDotEntries, nullptr);
        if (res < 0) {
            httpd_resp_set_type(request, "application/json");
            httpd_resp_sendstr(request, "{\"error\":\"scan failed\"}");
            return ESP_OK;
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

    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, json.str().c_str());
    return ESP_OK;
}

esp_err_t WebServerService::handleFsDownload(httpd_req_t* request) {
    std::string path;
    if (!getQueryParam(request, "path", path) || path.empty()) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "path required");
        return ESP_FAIL;
    }
    std::string norm = normalizePath(path);
    if (!isAllowedBasePath(norm) || !file::isFile(norm)) {
        LOGGER.warn("GET /fs/download - not found or invalid path: '{}' normalized: '{}'", path, norm);
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, getContentType(norm));
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
    httpd_resp_set_hdr(request, "Content-Disposition", disposition.c_str());
    FILE* fp = fopen(norm.c_str(), "rb");
    if (!fp) { httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "open failed"); return ESP_FAIL; }
    char buf[512]; size_t n;
    while ((n = fread(buf,1,sizeof(buf),fp))>0) {
        if (httpd_resp_send_chunk(request, buf, n) != ESP_OK) { fclose(fp); return ESP_FAIL; }
    }
    fclose(fp);
    httpd_resp_send_chunk(request, nullptr, 0);
    return ESP_OK;
}

esp_err_t WebServerService::handleFsUpload(httpd_req_t* request) {
    std::string path;

    // Log raw query and decoded path for diagnostics
    size_t qlen = httpd_req_get_url_query_len(request) + 1;
    if (qlen > 1) {
        std::unique_ptr<char[]> qbuf(new char[qlen]);
        if (httpd_req_get_url_query_str(request, qbuf.get(), qlen) == ESP_OK) {
            LOGGER.info("POST /fs/upload raw query: {}", qbuf.get());
        }
    }

    if (!getQueryParam(request, "path", path) || path.empty()) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "path required");
        return ESP_FAIL;
    }

    // Log decoded path and headers
    char content_type[64] = {0};
    httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type));
    std::string norm = normalizePath(path);
    LOGGER.info("POST /fs/upload decoded path: '{}' normalized: '{}' Content-Length: {} Content-Type: {}", path, norm, (int)request->content_len, content_type[0] ? content_type : "(null)");

    if (!isAllowedBasePath(norm)) {
        LOGGER.warn("POST /fs/upload - invalid path requested: '{}' normalized: '{}'", path, norm);
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "invalid path");
        return ESP_FAIL;
    }

    if (request->content_len > MAX_UPLOAD_SIZE) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "file too large");
        return ESP_FAIL;
    }

    // Ensure parent directory exists (after size check to avoid creating dirs for rejected uploads)
    if (!file::findOrCreateParentDirectory(norm, 0755)) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to create parent directory");
        return ESP_FAIL;
    }
    FILE* fp = fopen(norm.c_str(), "wb");
    if (!fp) { httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "open failed"); return ESP_FAIL; }
    char buf[512]; int remaining = request->content_len; int received=0;
    constexpr int MAX_TIMEOUT_RETRIES = 5;
    int timeout_retries = 0;
    while (remaining > 0) {
        int to_read = remaining > (int)sizeof(buf) ? (int)sizeof(buf) : remaining;
        int ret = httpd_req_recv(request, buf, to_read);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            // Timeout - retry with backoff
            timeout_retries++;
            if (timeout_retries >= MAX_TIMEOUT_RETRIES) {
                LOGGER.error("Upload recv timeout after {} retries", timeout_retries);
                fclose(fp);
                remove(norm.c_str());  // Clean up partial file
                httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "recv timeout");
                return ESP_FAIL;
            }
            LOGGER.warn("Upload recv timeout, retry {}/{}", timeout_retries, MAX_TIMEOUT_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(100 * timeout_retries)); // Linear backoff
            continue;
        }
        if (ret <= 0) {
            LOGGER.error("Upload recv failed with error {}", ret);
            fclose(fp);
            remove(norm.c_str());  // Clean up partial file
            httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
            return ESP_FAIL;
        }
        // Successful read - reset timeout counter
        timeout_retries = 0;
        size_t written = fwrite(buf, 1, ret, fp);
        if (written != (size_t)ret) {
            fclose(fp);
            remove(norm.c_str());
            httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
            return ESP_FAIL;
        }
        remaining -= ret;
        received += ret;
    }
    fclose(fp);
    httpd_resp_set_type(request, "text/plain");
    std::string msg = std::string("Uploaded ") + std::to_string(received) + " bytes";
    httpd_resp_sendstr(request, msg.c_str());
    return ESP_OK;
}

// Generic GET dispatcher for /fs/* URIs
esp_err_t WebServerService::handleFsGenericGet(httpd_req_t* request) {
    // Auth check for all /fs/* endpoints (file system access is sensitive)
    bool authPassed = false;
    esp_err_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    const char* uri = request->uri;
    if (uriMatches(uri, "/fs/list")) return handleFsList(request);
    if (uriMatches(uri, "/fs/download")) return handleFsDownload(request);
    if (uriMatches(uri, "/fs/tree")) return handleFsTree(request);
    LOGGER.warn("GET {} - not found in fs generic dispatcher", uri);
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "not found");
    return ESP_FAIL;
}

// Generic POST dispatcher for /fs/* URIs
esp_err_t WebServerService::handleFsGenericPost(httpd_req_t* request) {
    // Auth check for all /fs/* endpoints (file system access is sensitive)
    bool authPassed = false;
    esp_err_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    const char* uri = request->uri;
    if (uriMatches(uri, "/fs/mkdir")) return handleFsMkdir(request);
    if (uriMatches(uri, "/fs/delete")) return handleFsDelete(request);
    if (uriMatches(uri, "/fs/rename")) return handleFsRename(request);
    if (uriMatches(uri, "/fs/upload")) return handleFsUpload(request);
    LOGGER.warn("POST {} - not found in fs generic dispatcher", uri);
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "not found");
    return ESP_FAIL;
}

// Admin dispatcher for consolidated small POST endpoints (e.g. sync, reboot)
esp_err_t WebServerService::handleAdminPost(httpd_req_t* request) {
    // Auth check for all /admin/* endpoints (admin actions are sensitive)
    bool authPassed = false;
    esp_err_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    const char* uri = request->uri;
    if (strncmp(uri, "/admin/sync", 11) == 0) return handleSync(request);
    if (strncmp(uri, "/admin/reboot", 13) == 0) return handleReboot(request);
    LOGGER.info("POST {} - not found in admin dispatcher", uri);
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "not found");
    return ESP_FAIL;
}

// API GET dispatcher - returns JSON system information
// Note: /api/sysinfo is intentionally public for monitoring use cases
esp_err_t WebServerService::handleApiGet(httpd_req_t* request) {
    const char* uri = request->uri;

    // Public endpoint: sysinfo (basic device info for monitoring)
    if (strncmp(uri, "/api/sysinfo", 12) == 0) {
        return handleApiSysinfo(request);
    }

    // Protected endpoints require authentication
    bool authPassed = false;
    esp_err_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    // Auth-protected endpoints
    if (strncmp(uri, "/api/apps", 9) == 0) {
        return handleApiApps(request);
    }
    if (strncmp(uri, "/api/wifi", 9) == 0) {
        return handleApiWifi(request);
    }
    if (strncmp(uri, "/api/screenshot", 15) == 0) {
        return handleApiScreenshot(request);
    }

    LOGGER.warn("GET {} - not found in api dispatcher", uri);
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "not found");
    return ESP_FAIL;
}

// API POST dispatcher - all POST endpoints require authentication
esp_err_t WebServerService::handleApiPost(httpd_req_t* request) {
    bool authPassed = false;
    esp_err_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    const char* uri = request->uri;
    if (strncmp(uri, "/api/apps/run", 13) == 0) {
        return handleApiAppsRun(request);
    }
    if (strncmp(uri, "/api/apps/uninstall", 19) == 0) {
        return handleApiAppsUninstall(request);
    }

    LOGGER.warn("POST {} - not found in api dispatcher", uri);
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "not found");
    return ESP_FAIL;
}

// API PUT dispatcher - all PUT endpoints require authentication
esp_err_t WebServerService::handleApiPut(httpd_req_t* request) {
    bool authPassed = false;
    esp_err_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    const char* uri = request->uri;
    if (strncmp(uri, "/api/apps/install", 17) == 0) {
        return handleApiAppsInstall(request);
    }

    LOGGER.warn("PUT {} - not found in api dispatcher", uri);
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "not found");
    return ESP_FAIL;
}

esp_err_t WebServerService::handleApiSysinfo(httpd_req_t* request) {
    LOGGER.info("GET /api/sysinfo");

    std::ostringstream json;
    json << "{";

    // Firmware info
    json << "\"firmware\":{";
    json << "\"version\":\"" << TT_VERSION << "\",";
    json << "\"idf_version\":\"" << ESP_IDF_VERSION_MAJOR << "." << ESP_IDF_VERSION_MINOR << "." << ESP_IDF_VERSION_PATCH << "\"";
    json << "},";

    // Chip info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    json << "\"chip\":{";
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
    json << "},";

    // Memory - Internal heap
    size_t heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t heap_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t heap_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    json << "\"heap\":{";
    json << "\"free\":" << heap_free << ",";
    json << "\"total\":" << heap_total << ",";
    json << "\"min_free\":" << heap_min_free << ",";
    json << "\"largest_block\":" << heap_largest;
    json << "},";

    // Memory - PSRAM (external)
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    json << "\"psram\":{";
    json << "\"free\":" << psram_free << ",";
    json << "\"total\":" << psram_total << ",";
    json << "\"min_free\":" << psram_min_free << ",";
    json << "\"largest_block\":" << psram_largest;
    json << "},";

    // Storage info
    json << "\"storage\":{";
    uint64_t storage_total = 0, storage_free = 0;

    // Data partition
    json << "\"data\":{";
    if (esp_vfs_fat_info(file::MOUNT_POINT_DATA, &storage_total, &storage_free) == ESP_OK) {
        json << "\"free\":" << storage_free << ",";
        json << "\"total\":" << storage_total << ",";
        json << "\"mounted\":true";
    } else {
        json << "\"mounted\":false";
    }
    json << "},";

    // SD card - check all sdcard devices
    json << "\"sdcard\":{";
    bool sdcard_found = false;
    const auto sdcard_devices = hal::findDevices<hal::sdcard::SdCardDevice>(hal::Device::Type::SdCard);
    for (const auto& sdcard : sdcard_devices) {
        if (sdcard->isMounted() && esp_vfs_fat_info(sdcard->getMountPath().c_str(), &storage_total, &storage_free) == ESP_OK) {
            json << "\"free\":" << storage_free << ",";
            json << "\"total\":" << storage_total << ",";
            json << "\"mounted\":true";
            sdcard_found = true;
            break;
        }
    }
    if (!sdcard_found) {
        json << "\"mounted\":false";
    }
    json << "}";

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

    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, json.str().c_str());
    return ESP_OK;
}

// GET /api/apps - List installed apps
esp_err_t WebServerService::handleApiApps(httpd_req_t* request) {
    LOGGER.info("GET /api/apps");

    auto manifests = app::getAppManifests();

    std::ostringstream json;
    json << "{\"apps\":[";

    bool first = true;
    for (const auto& manifest : manifests) {
        if (!first) json << ",";
        first = false;

        json << "{";
        json << "\"id\":\"" << escapeJson(manifest->appId) << "\",";
        json << "\"name\":\"" << escapeJson(manifest->appName) << "\",";
        json << "\"version\":\"" << escapeJson(manifest->appVersionName) << "\",";

        const char* category = "user";
        if (manifest->appCategory == app::Category::System) category = "system";
        else if (manifest->appCategory == app::Category::Settings) category = "settings";
        json << "\"category\":\"" << category << "\",";

        json << "\"isExternal\":" << (manifest->appLocation.isExternal() ? "true" : "false") << ",";
        json << "\"hidden\":" << ((manifest->appFlags & app::AppManifest::Flags::Hidden) ? "true" : "false");

        if (!manifest->appIcon.empty()) {
            json << ",\"icon\":\"" << escapeJson(manifest->appIcon) << "\"";
        }

        json << "}";
    }

    json << "]}";

    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, json.str().c_str());
    return ESP_OK;
}

// POST /api/apps/run?id=xxx - Run an app
esp_err_t WebServerService::handleApiAppsRun(httpd_req_t* request) {
    LOGGER.info("POST /api/apps/run");

    std::string appId;
    if (!getQueryParam(request, "id", appId) || appId.empty()) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "id parameter required");
        return ESP_FAIL;
    }

    auto manifest = app::findAppManifestById(appId);
    if (!manifest) {
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "app not found");
        return ESP_FAIL;
    }

    // Stop if already running
    if (app::isRunning(appId)) {
        app::stopAll(appId);
    }

    app::start(appId);

    LOGGER.info("[200] /api/apps/run {}", appId);
    httpd_resp_sendstr(request, "ok");
    return ESP_OK;
}

// POST /api/apps/uninstall?id=xxx - Uninstall an app
esp_err_t WebServerService::handleApiAppsUninstall(httpd_req_t* request) {
    LOGGER.info("POST /api/apps/uninstall");

    std::string appId;
    if (!getQueryParam(request, "id", appId) || appId.empty()) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "id parameter required");
        return ESP_FAIL;
    }

    auto manifest = app::findAppManifestById(appId);
    if (!manifest) {
        LOGGER.info("[200] /api/apps/uninstall {} (app wasn't installed)", appId);
        httpd_resp_sendstr(request, "ok");
        return ESP_OK;
    }

    // Only allow uninstalling external apps
    if (manifest->appLocation.isInternal()) {
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "cannot uninstall system apps");
        return ESP_FAIL;
    }

    if (app::uninstall(appId)) {
        LOGGER.info("[200] /api/apps/uninstall {}", appId);
        httpd_resp_sendstr(request, "ok");
        return ESP_OK;
    } else {
        LOGGER.warn("[500] /api/apps/uninstall {}", appId);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "uninstall failed");
        return ESP_FAIL;
    }
}

// PUT /api/apps/install - Install an app from multipart form upload
esp_err_t WebServerService::handleApiAppsInstall(httpd_req_t* request) {
    LOGGER.info("PUT /api/apps/install");

    std::string boundary;
    if (!network::getMultiPartBoundaryOrSendError(request, boundary)) {
        return ESP_FAIL;
    }

    size_t content_left = request->content_len;
    constexpr size_t MAX_APP_UPLOAD_SIZE = 20 * 1024 * 1024;

    // Read headers until empty line (skip boundary line first)
    auto content_headers_data = network::receiveTextUntil(request, "\r\n\r\n");
    content_left -= content_headers_data.length();

    // Split headers into lines and filter empty ones
    auto content_headers = string::split(content_headers_data, "\r\n")
        | std::views::filter([](const std::string& line) {
            return line.length() > 0;
        })
        | std::ranges::to<std::vector>();

    auto content_disposition_map = network::parseContentDisposition(content_headers);
    if (content_disposition_map.empty()) {
        LOGGER.warn("parseContentDisposition returned empty map for: {}", content_headers_data);
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid content disposition");
        return ESP_FAIL;
    }

    auto filename_entry = content_disposition_map.find("filename");
    if (filename_entry == content_disposition_map.end()) {
        LOGGER.warn("filename not found in content disposition map");
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "filename parameter missing");
        return ESP_FAIL;
    }

    // Calculate file size
    auto boundary_and_newlines_after_file = std::format("\r\n--{}--\r\n", boundary);
    if (content_left <= boundary_and_newlines_after_file.length()) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid multipart payload");
        return ESP_FAIL;
    }

    auto file_size = content_left - boundary_and_newlines_after_file.length();
    if (file_size == 0 || file_size > MAX_APP_UPLOAD_SIZE) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "file too large");
        return ESP_FAIL;
    }

    // Create tmp directory
    const std::string tmp_path = getTempPath();
    if (!file::findOrCreateDirectory(tmp_path, 0777)) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to create temp directory");
        return ESP_FAIL;
    }

    std::string safe_name = file::getLastPathSegment(filename_entry->second);
    if (safe_name.empty() || safe_name.find("..") != std::string::npos ||
        safe_name.find('/') != std::string::npos || safe_name.find('\\') != std::string::npos) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid filename");
        return ESP_FAIL;
    }
    auto file_path = std::format("{}/{}", tmp_path, safe_name);

    if (network::receiveFile(request, file_size, file_path) != file_size) {
        file::deleteFile(file_path);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to save file");
        return ESP_FAIL;
    }

    content_left -= file_size;

    // Read and discard trailing boundary
    if (!network::readAndDiscardOrSendError(request, boundary_and_newlines_after_file)) {
        return ESP_FAIL;
    }

    // Install the app
    if (!app::install(file_path)) {
        file::deleteFile(file_path);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "installation failed");
        return ESP_FAIL;
    }

    // Cleanup temp file
    if (!file::deleteFile(file_path)) {
        LOGGER.warn("Failed to delete temp file {}", file_path);
    }

    LOGGER.info("[200] /api/apps/install -> {}", file_path);
    httpd_resp_sendstr(request, "ok");
    return ESP_OK;
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
esp_err_t WebServerService::handleApiWifi(httpd_req_t* request) {
    LOGGER.info("GET /api/wifi");

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

    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, json.str().c_str());
    return ESP_OK;
}

// GET /api/screenshot - Capture and return screenshot as PNG
// Screenshots are saved to SD card root (if available) or /data with incrementing numbers
esp_err_t WebServerService::handleApiScreenshot(httpd_req_t* request) {
    LOGGER.info("GET /api/screenshot");

#if TT_FEATURE_SCREENSHOT_ENABLED
    // Determine save location: prefer SD card root if mounted, otherwise /data
    std::string save_path;
    auto sdcard_devices = hal::findDevices<hal::sdcard::SdCardDevice>(hal::Device::Type::SdCard);
    for (const auto& sdcard : sdcard_devices) {
        if (sdcard->isMounted()) {
            save_path = sdcard->getMountPath();
            break;
        }
    }
    if (save_path.empty()) {
        save_path = file::MOUNT_POINT_DATA;
    }

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
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "no available screenshot slots");
        return ESP_FAIL;
    }

    LOGGER.info("Screenshot will be saved to: {}", screenshot_path);

    // LVGL's lodepng uses lv_fs which requires the "A:" prefix
    std::string lvgl_screenshot_path = lvgl::PATH_PREFIX + screenshot_path;

    // Capture screenshot using LVGL
    if (lvgl::lock(pdMS_TO_TICKS(100))) {
        bool success = lv_screenshot_create(lv_scr_act(), LV_100ASK_SCREENSHOT_SV_PNG, lvgl_screenshot_path.c_str());
        lvgl::unlock();

        if (!success) {
            LOGGER.error("lv_screenshot_create failed for path: {}", lvgl_screenshot_path);
            httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "screenshot capture failed");
            return ESP_FAIL;
        }
        LOGGER.info("Screenshot captured successfully");
    } else {
        LOGGER.error("Could not acquire LVGL lock within 100ms");
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "could not acquire LVGL lock");
        return ESP_FAIL;
    }

    // Send the file (use regular path for fopen, not LVGL path)
    httpd_resp_set_type(request, "image/png");

    FILE* fp = fopen(screenshot_path.c_str(), "rb");
    if (!fp) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to open screenshot");
        return ESP_FAIL;
    }

    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (httpd_resp_send_chunk(request, buf, n) != ESP_OK) {
            fclose(fp);
            return ESP_FAIL;
        }
    }
    fclose(fp);
    httpd_resp_send_chunk(request, nullptr, 0);

    // File is kept on storage (not deleted) for user access
    LOGGER.info("[200] /api/screenshot -> {}", screenshot_path);
    return ESP_OK;
#else
    httpd_resp_send_err(request, HTTPD_501_METHOD_NOT_IMPLEMENTED, "screenshot feature not enabled");
    return ESP_FAIL;
#endif
}

esp_err_t WebServerService::handleFsTree(httpd_req_t* request) {

    LOGGER.info("GET /fs/tree");

    std::ostringstream json;
    json << "{";
    // Gather mount points
    auto mounts = file::getMountPoints();
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

    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, json.str().c_str());
    return ESP_OK;
}

// Create a directory at the specified path (POST /fs/mkdir?path=/data/newdir)
esp_err_t WebServerService::handleFsMkdir(httpd_req_t* request) {
    std::string path;
    if (!getQueryParam(request, "path", path) || path.empty()) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "path required");
        return ESP_FAIL;
    }
    std::string norm = normalizePath(path);
    LOGGER.info("POST /fs/mkdir requested: '{}' normalized: '{}'", path, norm);
    if (!isAllowedBasePath(norm)) {
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "invalid path");
        return ESP_FAIL;
    }
    bool ok = file::findOrCreateDirectory(norm, 0755);
    if (!ok) { httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "mkdir failed"); return ESP_FAIL; }
    httpd_resp_sendstr(request, "ok");
    return ESP_OK;
}

static bool isRootMountPoint(const std::string& path) {
    return path == "/data" || path == "/sdcard";
}

// Delete a file or directory (POST /fs/delete?path=/data/foo)
esp_err_t WebServerService::handleFsDelete(httpd_req_t* request) {
    std::string path;
    if (!getQueryParam(request, "path", path) || path.empty()) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "path required");
        return ESP_FAIL;
    }
    std::string norm = normalizePath(path);
    LOGGER.info("POST /fs/delete requested: '{}' normalized: '{}'", path, norm);
    if (!isAllowedBasePath(norm)) {
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "invalid path");
        return ESP_FAIL;
    }
    if (isRootMountPoint(norm)) {
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "cannot delete mount point");
        return ESP_FAIL;
    }
    bool ok = true;
    if (file::isDirectory(norm)) ok = file::deleteRecursively(norm);
    else if (file::isFile(norm)) ok = file::deleteFile(norm);
    else ok = false;
    if (!ok) { httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "delete failed"); return ESP_FAIL; }
    httpd_resp_sendstr(request, "ok");
    return ESP_OK;
}

// Rename a file or folder (POST /fs/rename?path=/data/oldname&newName=newname)
esp_err_t WebServerService::handleFsRename(httpd_req_t* request) {
    std::string path;
    std::string newName;
    if (!getQueryParam(request, "path", path) || path.empty()) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "path required");
        return ESP_FAIL;
    }
    if (!getQueryParam(request, "newName", newName) || newName.empty()) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "newName required");
        return ESP_FAIL;
    }
    std::string norm = normalizePath(path);
    LOGGER.info("POST /fs/rename requested: '{}' normalized: '{}' -> newName: '{}'", path.c_str(), norm.c_str(), newName.c_str());
    if (!isAllowedBasePath(norm)) {
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "invalid path");
        return ESP_FAIL;
    }

    // Basic validation of newName: must not contain path separators or '..'
    // Trim whitespace from newName
    auto trim = [](std::string& s){ size_t st=0; while (st<s.size() && isspace((unsigned char)s[st])) ++st; size_t ed=s.size(); while (ed>st && isspace((unsigned char)s[ed-1])) --ed; s = s.substr(st, ed-st); };
    trim(newName);
    if (newName.empty() || newName.find('/') != std::string::npos || newName.find('\\') != std::string::npos || newName.find("..") != std::string::npos) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid newName");
        return ESP_FAIL;
    }

    // compute parent directory
    std::string parent = "/";
    size_t pos = norm.find_last_of('/');
    if (pos != std::string::npos) {
        parent = (pos == 0) ? std::string("/") : norm.substr(0, pos);
    }

    if (!isAllowedBasePath(parent)) {
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "invalid target parent");
        return ESP_FAIL;
    }

    std::string target = file::getChildPath(parent, newName);

    // Prevent overwrite: fail if target exists
    if (file::isFile(target) || file::isDirectory(target)) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "target exists");
        return ESP_FAIL;
    }

    // perform rename
    int r = rename(norm.c_str(), target.c_str());
    if (r != 0) {
        int e = errno;
        LOGGER.warn("rename failed errno={} ({}) -> {} -> {}", e, strerror(e), norm, target);
        // Return errno string to client to aid debugging
        std::string msg = std::string("rename failed: ") + strerror(e);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, msg.c_str());
        return ESP_FAIL;
    }
    httpd_resp_sendstr(request, "ok");
    return ESP_OK;
}

// endregion

esp_err_t WebServerService::handleSync(httpd_req_t* request) {
    
    LOGGER.info("POST /sync");
    
    bool success = syncAssets();
    
    if (success) {
        httpd_resp_sendstr(request, "Assets synchronized successfully");
    } else {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Asset sync failed");
    }
    
    return success ? ESP_OK : ESP_FAIL;
}

esp_err_t WebServerService::handleReboot(httpd_req_t* request) {
    
    LOGGER.info("POST /reboot");
    
    httpd_resp_sendstr(request, "Rebooting...");
    
    // Reboot after a short delay to allow response to be sent
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK; // Unreachable, but satisfies function signature
}

esp_err_t WebServerService::handleAssets(httpd_req_t* request) {
    // Auth check for UI access control
    bool authPassed = false;
    esp_err_t authResult = validateRequestAuth(request, authPassed);
    if (!authPassed) {
        return authResult;
    }

    const char* uri = request->uri;
    LOGGER.info("GET {}", uri);

    // Special case: serve favicon from system assets
    if (strcmp(uri, "/favicon.ico") == 0) {
        const char* faviconPath = "/data/system/spinner.png";
        if (file::isFile(faviconPath)) {
            httpd_resp_set_type(request, "image/png");
            httpd_resp_set_hdr(request, "Cache-Control", "public, max-age=86400");

            auto lock = file::getLock(faviconPath);
            lock->lock(portMAX_DELAY);

            FILE* fp = fopen(faviconPath, "rb");
            if (fp) {
                char buffer[512];
                size_t bytesRead;
                while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
                    if (httpd_resp_send_chunk(request, buffer, bytesRead) != ESP_OK) {
                        fclose(fp);
                        lock->unlock();
                        return ESP_FAIL;
                    }
                }
                fclose(fp);
                lock->unlock();
                httpd_resp_send_chunk(request, nullptr, 0);
                LOGGER.info("[200] {} (favicon)", uri);
                return ESP_OK;
            }
            lock->unlock();
        }
        // If favicon not found, return 404 silently (browsers handle this gracefully)
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }

    // Special case: if requesting dashboard.html but it doesn't exist, serve default.html
    std::string requestedPath = uri;
    if (auto qpos = requestedPath.find('?'); qpos != std::string::npos) {
        requestedPath = requestedPath.substr(0, qpos);
    }
    requestedPath = normalizePath(requestedPath);
    if (requestedPath == "/.." || requestedPath.ends_with("/..") || requestedPath.find("/../") != std::string::npos) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid path");
        return ESP_FAIL;
    }

    std::string dataPath = std::string("/data/webserver") + requestedPath;
    
    if (requestedPath == "/dashboard.html" && !file::isFile(dataPath.c_str())) {
        // Dashboard doesn't exist, try default.html
        dataPath = "/data/webserver/default.html";
        LOGGER.info("dashboard.html not found, serving default.html");
    }
    
    // Try to serve from Data partition first
    if (file::isFile(dataPath.c_str())) {
        httpd_resp_set_type(request, getContentType(dataPath));
        
        // Read and send file using standard C FILE* operations
        auto lock = file::getLock(dataPath);
        lock->lock(portMAX_DELAY);
        
        FILE* fp = fopen(dataPath.c_str(), "rb");
        if (fp) {
            char buffer[512];
            size_t bytesRead;
            while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
                if (httpd_resp_send_chunk(request, buffer, bytesRead) != ESP_OK) {
                    fclose(fp);
                    lock->unlock();
                    return ESP_FAIL;
                }
            }
            fclose(fp);
            lock->unlock();

            httpd_resp_send_chunk(request, nullptr, 0);  // End of chunks
            LOGGER.info("[200] {} (from Data)", uri);
            return ESP_OK;
        }
        lock->unlock();
    }

    // Fallback to SD card
    std::string sdPath = std::string("/sdcard/tactility/webserver") + requestedPath;
    if (file::isFile(sdPath.c_str())) {
        httpd_resp_set_type(request, getContentType(sdPath));
        
        auto lock = file::getLock(sdPath);
        lock->lock(portMAX_DELAY);
        
        FILE* fp = fopen(sdPath.c_str(), "rb");
        if (fp) {
            char buffer[512];
            size_t bytesRead;
            while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
                if (httpd_resp_send_chunk(request, buffer, bytesRead) != ESP_OK) {
                    fclose(fp);
                    lock->unlock();
                    return ESP_FAIL;
                }
            }
            fclose(fp);
            lock->unlock();
            
            httpd_resp_send_chunk(request, nullptr, 0);  // End of chunks
            LOGGER.info("[200] {} (from SD)", uri);
            return ESP_OK;
        }
        lock->unlock();
    }
    
    // File not found
    LOGGER.warn("[404] {}", uri);
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "File not found");
    return ESP_FAIL;
}

extern const ServiceManifest manifest = {
    .id = "WebServer",
    .createService = create<WebServerService>
};

void setWebServerEnabled(bool enabled) {
    WebServerService* instance = g_webServerInstance.load();
    if (instance != nullptr) {
        instance->setEnabled(enabled);
        // Don't log here - startServer()/stopServer() already log the actual result
    } else {
        LOGGER.warn("WebServer service not available, cannot {}", enabled ? "start" : "stop");
    }
}

} // namespace

#endif // ESP_PLATFORM
