#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/service/wifi/Wifi.h>

#include <tactility/check.h>
#include <Tactility/EventGroup.h>
#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/Tactility.h>
#include <Tactility/Timer.h>
#include <Tactility/kernel/SystemEvents.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/wifi/WifiBootSplashInit.h>
#include <Tactility/service/wifi/WifiGlobals.h>
#include <Tactility/service/wifi/WifiSettings.h>

#include <lwip/esp_netif_net_stack.h>
#include <freertos/FreeRTOS.h>
#include <atomic>
#include <cstring>
#include <sys/cdefs.h>

namespace tt::service::wifi {

static const auto LOGGER = Logger("WifiService");

constexpr auto WIFI_CONNECTED_BIT = BIT0;
constexpr auto WIFI_FAIL_BIT = BIT1;
constexpr auto AUTO_SCAN_INTERVAL = 10000; // ms

// Forward declarations
class Wifi;
static void scan_list_free_safely(std::shared_ptr<Wifi> wifi);
// Methods for main thread dispatcher
static void dispatchAutoConnect(std::shared_ptr<Wifi> wifi);
static void dispatchEnable(std::shared_ptr<Wifi> wifi);
static void dispatchDisable(std::shared_ptr<Wifi> wifi);
static void dispatchScan(std::shared_ptr<Wifi> wifi);
static void dispatchConnect(std::shared_ptr<Wifi> wifi);
static void dispatchDisconnectButKeepActive(std::shared_ptr<Wifi> wifi);

class Wifi {

    std::atomic<RadioState> radio_state = RadioState::Off;
    bool scan_active = false;
    bool secure_connection = false;

public:

    /** @brief Locking mechanism for modifying the Wifi instance */
    RecursiveMutex radioMutex;
    RecursiveMutex dataMutex;
    std::unique_ptr<Timer> autoConnectTimer;
    /** @brief The public event bus */
    std::shared_ptr<PubSub<WifiEvent>> pubsub = std::make_shared<PubSub<WifiEvent>>();
    // TODO: Deal with messages that come in while an action is ongoing
    // for example: when scanning and you turn off the radio, the scan should probably stop or turning off
    // the radio should disable the on/off button in the app as it is pending.
    /** @brief The network interface when wifi is started */
    esp_netif_t* _Nullable netif = nullptr;
    /** @brief Scanning results */
    wifi_ap_record_t* _Nullable scan_list = nullptr;
    /** @brief The current item count in scan_list (-1 when scan_list is NULL) */
    uint16_t scan_list_count = 0;
    /** @brief Maximum amount of records to scan (value > 0) */
    uint16_t scan_list_limit = TT_WIFI_SCAN_RECORD_LIMIT;
    /** @brief when we last requested a scan. Loops around every 50 days. */
    TickType_t last_scan_time = kernel::MAX_TICKS;
    esp_event_handler_instance_t event_handler_any_id = nullptr;
    esp_event_handler_instance_t event_handler_got_ip = nullptr;
    EventGroup connection_wait_flags;
    settings::WifiApSettings connection_target;
    bool pause_auto_connect = false; // Pause when manually disconnecting until manually connecting again
    bool connection_target_remember = false; // Whether to store the connection_target on successful connection or not
    esp_netif_ip_info_t ip_info;
    kernel::SystemEventSubscription bootEventSubscription = kernel::NoSystemEventSubscription;

    RadioState getRadioState() const {
        auto lock = dataMutex.asScopedLock();
        lock.lock();
        // TODO: Handle lock failure
        return radio_state;
    }

    void setRadioState(RadioState newState) {
        auto lock = dataMutex.asScopedLock();
        lock.lock();
        // TODO: Handle lock failure
        radio_state = newState;
    }

    bool isScanning() const {
        auto lock = dataMutex.asScopedLock();
        lock.lock();
        // TODO: Handle lock failure
        return scan_active;
    }

    void setScanning(bool newState) {
        auto lock = dataMutex.asScopedLock();
        lock.lock();
        // TODO: Handle lock failure
        scan_active = newState;
    }

    bool isScanActive() const {
        auto lock = dataMutex.asScopedLock();
        lock.lock();
        return scan_active;
    }

    void setScanActive(bool newState) {
        auto lock = dataMutex.asScopedLock();
        lock.lock();
        scan_active = newState;
    }

    bool isSecureConnection() const {
        auto lock = dataMutex.asScopedLock();
        lock.lock();
        return secure_connection;
    }

    void setSecureConnection(bool newState) {
        auto lock = dataMutex.asScopedLock();
        lock.lock();
        secure_connection = newState;
    }
};

static std::shared_ptr<Wifi> wifi_singleton;


// region Public functions

std::shared_ptr<PubSub<WifiEvent>> getPubsub() {
    auto wifi = wifi_singleton;
    if (wifi == nullptr) {
        check(false, "Service not running");
    }

    return wifi->pubsub;
}

RadioState getRadioState() {
    auto wifi = wifi_singleton;
    if (wifi != nullptr) {
        return wifi->getRadioState();
    } else {
        return RadioState::Off;
    }
}

std::string getConnectionTarget() {
    auto wifi = wifi_singleton;
    if (wifi == nullptr) {
        return "";
    }

    RadioState state = wifi->getRadioState();
    if (
        state != RadioState::ConnectionPending &&
            state != RadioState::ConnectionActive
    ) {
        return "";
    }

    return wifi->connection_target.ssid;
}

void scan() {
    LOGGER.info("scan()");
    auto wifi = wifi_singleton;
    if (wifi == nullptr) {
        return;
    }

    getMainDispatcher().dispatch([wifi]() { dispatchScan(wifi); });
}

bool isScanning() {
    auto wifi = wifi_singleton;
    if (wifi == nullptr) {
        return false;
    } else {
        return wifi->isScanActive();
    }
}

void connect(const settings::WifiApSettings& ap, bool remember) {
    LOGGER.info("connect({}, {})", ap.ssid, remember);
    auto wifi = wifi_singleton;
    if (wifi == nullptr) {
        return;
    }

    auto lock = wifi->dataMutex.asScopedLock();
    if (!lock.lock(10 / portTICK_PERIOD_MS)) {
        return;
    }

    // Stop auto-connecting until the connection is established
    wifi->pause_auto_connect = true;
    wifi->connection_target = ap;
    wifi->connection_target_remember = remember;

    if (wifi->getRadioState() == RadioState::Off) {
        getMainDispatcher().dispatch([wifi] { dispatchEnable(wifi); });
    }

    getMainDispatcher().dispatch([wifi] { dispatchConnect(wifi); });
}

void disconnect() {
    LOGGER.info("disconnect()");
    auto wifi = wifi_singleton;
    if (wifi == nullptr) {
        return;
    }

    auto lock = wifi->dataMutex.asScopedLock();
    if (!lock.lock(10 / portTICK_PERIOD_MS)) {
        return;
    }

    wifi->connection_target = settings::WifiApSettings("", "");
    // Manual disconnect (e.g. via app) should stop auto-connecting until a new connection is established
    wifi->pause_auto_connect = true;
    getMainDispatcher().dispatch([wifi]() { dispatchDisconnectButKeepActive(wifi); });
}

void clearIp() {
    auto wifi = wifi_singleton;
    if (wifi == nullptr) {
        return;
    }

    auto lock = wifi->dataMutex.asScopedLock();
    if (!lock.lock(10 / portTICK_PERIOD_MS)) {
        return;
    }

    memset(&wifi->ip_info, 0, sizeof(esp_netif_ip_info_t));
}
void setScanRecords(uint16_t records) {
    LOGGER.info("setScanRecords({})", records);
    auto wifi = wifi_singleton;
    if (wifi == nullptr) {
        return;
    }

    auto lock = wifi->dataMutex.asScopedLock();
    if (!lock.lock(10 / portTICK_PERIOD_MS)) {
        return;
    }

    if (records != wifi->scan_list_limit) {
        scan_list_free_safely(wifi);
        wifi->scan_list_limit = records;
    }
}

std::vector<ApRecord> getScanResults() {
    LOGGER.info("getScanResults()");
    auto wifi = wifi_singleton;

    std::vector<ApRecord> records;

    if (wifi == nullptr) {
        return records;
    }

    auto lock = wifi->dataMutex.asScopedLock();
    if (!lock.lock(10 / portTICK_PERIOD_MS)) {
        return records;
    }

    if (wifi->scan_list_count > 0) {
        uint16_t i = 0;
        for (; i < wifi->scan_list_count; ++i) {
            const auto& scanned_item = wifi->scan_list[i];
            records.push_back((ApRecord) {
                .ssid = reinterpret_cast<const char*>(scanned_item.ssid),
                .rssi = scanned_item.rssi,
                .channel = scanned_item.primary,
                .auth_mode = scanned_item.authmode
            });
        }
    }

    return records;
}

void setEnabled(bool enabled) {
    LOGGER.info("setEnabled({})", enabled);
    auto wifi = wifi_singleton;
    if (wifi == nullptr) {
        return;
    }

    auto lock = wifi->dataMutex.asScopedLock();
    if (!lock.lock(10 / portTICK_PERIOD_MS)) {
        return;
    }

    if (enabled) {
        getMainDispatcher().dispatch([wifi] { dispatchEnable(wifi); });
    } else {
        getMainDispatcher().dispatch([wifi] { dispatchDisable(wifi); });
    }

    wifi->pause_auto_connect = false;
    wifi->last_scan_time = 0;
}

bool isConnectionSecure() {
    auto wifi = wifi_singleton;
    if (wifi == nullptr) {
        return false;
    }

    auto lock = wifi->dataMutex.asScopedLock();
    if (!lock.lock(10 / portTICK_PERIOD_MS)) {
        return false;
    }

    return wifi->isSecureConnection();
}

int getRssi() {
    assert(wifi_singleton);
    static int rssi = 0;
    if (esp_wifi_sta_get_rssi(&rssi) == ESP_OK) {
        return rssi;
    } else {
        return 1;
    }
}

// endregion Public functions

static void scan_list_alloc(std::shared_ptr<Wifi> wifi) {
    auto lock = wifi->dataMutex.asScopedLock();
    if (lock.lock()) {
        assert(wifi->scan_list == nullptr);
        wifi->scan_list = static_cast<wifi_ap_record_t*>(malloc(sizeof(wifi_ap_record_t) * wifi->scan_list_limit));
        wifi->scan_list_count = 0;
    }
}

static void scan_list_alloc_safely(std::shared_ptr<Wifi> wifi) {
    auto lock = wifi->dataMutex.asScopedLock();
    if (lock.lock()) {
        if (wifi->scan_list == nullptr) {
            scan_list_alloc(wifi);
        }
    }
}

static void scan_list_free(std::shared_ptr<Wifi> wifi) {
    auto lock = wifi->dataMutex.asScopedLock();
    if (lock.lock()) {
        assert(wifi->scan_list != nullptr);
        free(wifi->scan_list);
        wifi->scan_list = nullptr;
        wifi->scan_list_count = 0;
    }
}

static void scan_list_free_safely(std::shared_ptr<Wifi> wifi) {
    auto lock = wifi->dataMutex.asScopedLock();
    if (lock.lock()) {
        if (wifi->scan_list != nullptr) {
            scan_list_free(wifi);
        }
    }
}

static void publish_event(std::shared_ptr<Wifi> wifi, WifiEvent event) {
    auto lock = wifi->dataMutex.asScopedLock();
    if (lock.lock()) {
        wifi->pubsub->publish(event);
    }
}

static bool copy_scan_list(std::shared_ptr<Wifi> wifi) {
    auto state = wifi->getRadioState();
    bool can_fetch_results = (state == RadioState::On || state == RadioState::ConnectionActive) &&
                             wifi->isScanActive();

    if (!can_fetch_results) {
        LOGGER.info("Skip scan result fetching");
        return false;
    }

    auto lock = wifi->dataMutex.asScopedLock();
    if (!lock.lock()) {
        return false;
    }

    // Create scan list if it does not exist
    scan_list_alloc_safely(wifi);
    wifi->scan_list_count = 0;
    uint16_t record_count = wifi->scan_list_limit;
    esp_err_t scan_result = esp_wifi_scan_get_ap_records(&record_count, wifi->scan_list);
    if (scan_result == ESP_OK) {
        uint16_t safe_record_count = std::min(wifi->scan_list_limit, record_count);
        wifi->scan_list_count = safe_record_count;
        LOGGER.info("Scanned {} APs. Showing {}:", record_count, safe_record_count);
        for (uint16_t i = 0; i < safe_record_count; i++) {
            wifi_ap_record_t* record = &wifi->scan_list[i];
            if (record->ssid[0] != 0 && record->primary != 0) {
                LOGGER.info(" - SSID {}, RSSI {}, channel {}, BSSID {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                    reinterpret_cast<const char*>(record->ssid),
                    record->rssi,
                    record->primary,
                    record->bssid[0],
                    record->bssid[1],
                    record->bssid[2],
                    record->bssid[3],
                    record->bssid[4],
                    record->bssid[5]
                );
            } else {
                LOGGER.info(" - (missing channel info)"); // Behaviour on on P4 with C6
            }
        }
        return true;
    } else {
        LOGGER.info("Failed to get scanned records: {}", esp_err_to_name(scan_result));
        return false;
    }
}

static bool find_auto_connect_ap(std::shared_ptr<Wifi> wifi, settings::WifiApSettings& settings) {
    LOGGER.info("find_auto_connect_ap()");
    auto lock = wifi->dataMutex.asScopedLock();
    if (lock.lock(10 / portTICK_PERIOD_MS)) {
        for (int i = 0; i < wifi->scan_list_count; ++i) {
            auto ssid = reinterpret_cast<const char*>(wifi->scan_list[i].ssid);
            if (settings::contains(ssid)) {
                static_assert(sizeof(wifi->scan_list[i].ssid) == (TT_WIFI_SSID_LIMIT + 1), "SSID size mismatch");
                if (settings::load(ssid, settings)) {
                    if (settings.autoConnect) {
                        return true;
                    }
                } else {
                    LOGGER.error("Failed to load credentials for ssid {}", ssid);
                }
                break;
            }
        }
    }

    return false;
}

static void dispatchAutoConnect(std::shared_ptr<Wifi> wifi) {
    LOGGER.info("dispatchAutoConnect()");

    settings::WifiApSettings settings;
    if (find_auto_connect_ap(wifi, settings)) {
        LOGGER.info("Auto-connecting to {}", settings.ssid);
        connect(settings, false);
        // TODO: We currently have to manually reset it because connect() sets it.
        // connect() assumes it's only being called by the user and not internally, so it disables auto-connect
        wifi->pause_auto_connect = false;
    }
}

static void eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto wifi = wifi_singleton;
    if (wifi == nullptr) {
        LOGGER.error("eventHandler: no wifi instance");
        return;
    }

    if (event_base == WIFI_EVENT) {
        LOGGER.info("eventHandler: WIFI_EVENT {}", event_id);
    } else if (event_base == IP_EVENT) {
        LOGGER.info("eventHandler: IP_EVENT {}", event_id);
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        LOGGER.info("eventHandler: STA_START");
        if (wifi->getRadioState() == RadioState::ConnectionPending) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        LOGGER.info("eventHandler: STA_DISCONNECTED");
        clearIp();
        switch (wifi->getRadioState()) {
            case RadioState::ConnectionPending:
                wifi->connection_wait_flags.set(WIFI_FAIL_BIT);
                break;
            case RadioState::On:
                // Ensure we can reconnect again
                wifi->pause_auto_connect = false;
                break;
            default:
                break;
        }
        wifi->setRadioState(RadioState::On);
        publish_event(wifi, WifiEvent::Disconnected);
        kernel::publishSystemEvent(kernel::SystemEvent::NetworkDisconnected);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        memcpy(&wifi->ip_info, &event->ip_info, sizeof(esp_netif_ip_info_t));
        LOGGER.info("eventHandler: got ip: {}.{}.{}.{}", IP2STR(&event->ip_info.ip));
        if (wifi->getRadioState() == RadioState::ConnectionPending) {
            wifi->connection_wait_flags.set(WIFI_CONNECTED_BIT);
            // We resume auto-connecting only when there was an explicit request by the user for the connection
            // TODO: Make thread-safe
            wifi->pause_auto_connect = false; // Resume auto-connection
        }
        kernel::publishSystemEvent(kernel::SystemEvent::NetworkConnected);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        auto* event = static_cast<wifi_event_sta_scan_done_t*>(event_data);
        LOGGER.info("eventHandler: wifi scanning done (scan id {})", event->scan_id);
        bool copied_list = copy_scan_list(wifi);

        auto state = wifi->getRadioState();
        if (
            state != RadioState::Off &&
            state != RadioState::OffPending
        ) {
            wifi->setScanActive(false);
            esp_wifi_scan_stop();
        }

        publish_event(wifi_singleton, WifiEvent::ScanFinished);
        LOGGER.info("eventHandler: Finished scan");

        if (copied_list && wifi_singleton->getRadioState() == RadioState::On && !wifi->pause_auto_connect) {
            getMainDispatcher().dispatch([wifi]() { dispatchAutoConnect(wifi); });
        }
    }
}

static void dispatchEnable(std::shared_ptr<Wifi> wifi) {
    LOGGER.info("dispatchEnable()");

    RadioState state = wifi->getRadioState();
    if (
        state == RadioState::On ||
        state == RadioState::OnPending ||
        state == RadioState::OffPending
    ) {
        LOGGER.warn("Can't enable from current state");
        return;
    }

    auto lock = wifi->radioMutex.asScopedLock();
    if (lock.lock(50 / portTICK_PERIOD_MS)) {
        LOGGER.info("Enabling");
        wifi->setRadioState(RadioState::OnPending);
        publish_event(wifi, WifiEvent::RadioStateOnPending);

        if (wifi->netif != nullptr) {
            esp_netif_destroy(wifi->netif);
        }
        wifi->netif = esp_netif_create_default_wifi_sta();

        // Warning: this is the memory-intensive operation
        // It uses over 117kB of RAM with default settings for S3 on IDF v5.1.2
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t init_result = esp_wifi_init(&config);
        if (init_result != ESP_OK) {
            LOGGER.error("Wifi init failed");
            if (init_result == ESP_ERR_NO_MEM) {
                LOGGER.error("Insufficient memory");
            }
            wifi->setRadioState(RadioState::Off);
            publish_event(wifi, WifiEvent::RadioStateOff);
            return;
        }

        esp_wifi_set_storage(WIFI_STORAGE_RAM);

        // TODO: don't crash on check failure
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &eventHandler,
            nullptr,
            &wifi->event_handler_any_id
        ));

        // TODO: don't crash on check failure
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &eventHandler,
            nullptr,
            &wifi->event_handler_got_ip
        ));

        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
            LOGGER.error("Wifi mode setting failed");
            wifi->setRadioState(RadioState::Off);
            esp_wifi_deinit();
            publish_event(wifi, WifiEvent::RadioStateOff);
            return;
        }

        esp_err_t start_result = esp_wifi_start();
        if (start_result != ESP_OK) {
            LOGGER.error("Wifi start failed");
            if (start_result == ESP_ERR_NO_MEM) {
                LOGGER.error("Insufficient memory");
            }
            wifi->setRadioState(RadioState::Off);
            esp_wifi_set_mode(WIFI_MODE_NULL);
            esp_wifi_deinit();
            publish_event(wifi, WifiEvent::RadioStateOff);
            return;
        }

        wifi->setRadioState(RadioState::On);
        publish_event(wifi, WifiEvent::RadioStateOn);

        wifi->pause_auto_connect = false;

        LOGGER.info("Enabled");
    } else {
        LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED);
    }
}

static void dispatchDisable(std::shared_ptr<Wifi> wifi) {
    LOGGER.info("dispatchDisable()");
    auto lock = wifi->radioMutex.asScopedLock();

    if (!lock.lock(50 / portTICK_PERIOD_MS)) {
        LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "disable()");
        return;
    }

    RadioState state = wifi->getRadioState();
    if (
        state == RadioState::Off ||
        state == RadioState::OffPending ||
        state == RadioState::OnPending
    ) {
        LOGGER.warn("Can't disable from current state");
        return;
    }

    LOGGER.info("Disabling");
    wifi->setRadioState(RadioState::OffPending);
    publish_event(wifi, WifiEvent::RadioStateOffPending);

    // Free up scan list memory
    scan_list_free_safely(wifi_singleton);

    if (esp_wifi_stop() != ESP_OK) {
        LOGGER.error("Failed to stop radio");
        wifi->setRadioState(RadioState::On);
        publish_event(wifi, WifiEvent::RadioStateOn);
        return;
    }

    if (esp_wifi_set_mode(WIFI_MODE_NULL) != ESP_OK) {
        LOGGER.error("Failed to unset mode");
    }

    if (esp_event_handler_instance_unregister(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi->event_handler_any_id
    ) != ESP_OK) {
        LOGGER.error("Failed to unregister id event handler");
    }

    if (esp_event_handler_instance_unregister(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        wifi->event_handler_got_ip
    ) != ESP_OK) {
        LOGGER.error("Failed to unregister ip event handler");
    }

    if (esp_wifi_deinit() != ESP_OK) {
        LOGGER.error("Failed to deinit");
    }

    assert(wifi->netif != nullptr);
    esp_netif_destroy(wifi->netif);
    wifi->netif = nullptr;
    wifi->setScanActive(false);
    wifi->setRadioState(RadioState::Off);
    publish_event(wifi, WifiEvent::RadioStateOff);
    LOGGER.info("Disabled");
}

static void dispatchScan(std::shared_ptr<Wifi> wifi) {
    LOGGER.info("dispatchScan()");
    auto lock = wifi->radioMutex.asScopedLock();

    if (!lock.lock(10 / portTICK_PERIOD_MS)) {
        LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return;
    }

    RadioState state = wifi->getRadioState();
    if (state != RadioState::On && state != RadioState::ConnectionActive && state != RadioState::ConnectionPending) {
        LOGGER.warn("Scan unavailable: wifi not enabled");
        return;
    }

    if (wifi->isScanActive()) {
        LOGGER.warn("Scan already pending");
        return;
    }

    // TODO: Thread safety
    wifi->last_scan_time = tt::kernel::getTicks();

    if (esp_wifi_scan_start(nullptr, false) != ESP_OK) {
        LOGGER.info("Can't start scan");
        return;
    }

    LOGGER.info("Starting scan");
    wifi->setScanActive(true);
    publish_event(wifi, WifiEvent::ScanStarted);
}

static void dispatchConnect(std::shared_ptr<Wifi> wifi) {
    LOGGER.info("dispatchConnect()");
    auto lock = wifi->radioMutex.asScopedLock();

    if (!lock.lock(50 / portTICK_PERIOD_MS)) {
        LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "dispatchConnect()");
        return;
    }

    LOGGER.info("Connecting to {}", wifi->connection_target.ssid);

    // Stop radio first, if needed
    RadioState radio_state = wifi->getRadioState();
    if (
        radio_state == RadioState::On ||
        radio_state == RadioState::ConnectionActive ||
        radio_state == RadioState::ConnectionPending
    ) {
        LOGGER.info("Connecting: Stopping radio first");
        esp_err_t stop_result = esp_wifi_stop();
        wifi->setScanActive(false);
        if (stop_result != ESP_OK) {
            LOGGER.error("Connecting: Failed to disconnect ({})", esp_err_to_name(stop_result));
            return;
        }
    }

    wifi->setScanActive(false);

    wifi->setRadioState(RadioState::ConnectionPending);

    publish_event(wifi, WifiEvent::ConnectionPending);

    wifi_config_t config;
    memset(&config, 0, sizeof(wifi_config_t));
    config.sta.channel = wifi_singleton->connection_target.channel;
    config.sta.scan_method = WIFI_FAST_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.rssi = -127;
    config.sta.pmf_cfg.capable = true;

    memcpy(config.sta.ssid, wifi_singleton->connection_target.ssid.c_str(), wifi_singleton->connection_target.ssid.size());

    if (wifi_singleton->connection_target.password[0] != 0x00) {
        memcpy(config.sta.password, wifi_singleton->connection_target.password.c_str(), wifi_singleton->connection_target.password.size());
        config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    LOGGER.info("esp_wifi_set_config()");
    esp_err_t set_config_result = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (set_config_result != ESP_OK) {
        wifi->setRadioState(RadioState::On);
        LOGGER.error("Failed to set wifi config ({})", esp_err_to_name(set_config_result));
        publish_event(wifi, WifiEvent::ConnectionFailed);
        return;
    }

    LOGGER.info("esp_wifi_start()");
    esp_err_t wifi_start_result = esp_wifi_start();
    if (wifi_start_result != ESP_OK) {
        wifi->setRadioState(RadioState::On);
        LOGGER.error("Failed to start wifi to begin connecting ({})", esp_err_to_name(wifi_start_result));
        publish_event(wifi, WifiEvent::ConnectionFailed);
        return;
    }

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT)
     * or connection failed for the maximum number of re-tries (WIFI_FAIL_BIT).
     * The bits are set by wifi_event_handler() */
    uint32_t flags;
    if (wifi_singleton->connection_wait_flags.wait(WIFI_FAIL_BIT | WIFI_CONNECTED_BIT, false, true, &flags, kernel::MAX_TICKS)) {
        LOGGER.info("Waiting for EventGroup by event_handler()");

        if (flags & WIFI_CONNECTED_BIT) {
            wifi->setSecureConnection(config.sta.password[0] != 0x00U);
            wifi->setRadioState(RadioState::ConnectionActive);
            publish_event(wifi, WifiEvent::ConnectionSuccess);
            LOGGER.info("Connected to {}", wifi->connection_target.ssid.c_str());
            if (wifi->connection_target_remember) {
                if (!settings::save(wifi->connection_target)) {
                    LOGGER.error("Failed to store credentials");
                } else {
                    LOGGER.info("Stored credentials");
                }
            }
        } else if (flags & WIFI_FAIL_BIT) {
            wifi->setRadioState(RadioState::On);
            publish_event(wifi, WifiEvent::ConnectionFailed);
            LOGGER.info("Failed to connect to {}", wifi->connection_target.ssid.c_str());
        } else {
            wifi->setRadioState(RadioState::On);
            publish_event(wifi, WifiEvent::ConnectionFailed);
            LOGGER.error("UNEXPECTED EVENT");
        }

        wifi_singleton->connection_wait_flags.clear(WIFI_FAIL_BIT | WIFI_CONNECTED_BIT);
    }
}

static void dispatchDisconnectButKeepActive(std::shared_ptr<Wifi> wifi) {
    LOGGER.info("dispatchDisconnectButKeepActive()");
    auto lock = wifi->radioMutex.asScopedLock();

    if (!lock.lock(50 / portTICK_PERIOD_MS)) {
        LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED);
        return;
    }

    esp_err_t stop_result = esp_wifi_stop();
    if (stop_result != ESP_OK) {
        LOGGER.error("Failed to disconnect ({})", esp_err_to_name(stop_result));
        return;
    }

    wifi_config_t config;
    memset(&config, 0, sizeof(wifi_config_t));
    config.sta.channel = wifi_singleton->connection_target.channel;
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.rssi = -127;
    config.sta.pmf_cfg.capable = true;

    esp_err_t set_config_result = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (set_config_result != ESP_OK) {
        // TODO: disable radio, because radio state is in limbo between off and on
        wifi->setRadioState(RadioState::Off);
        LOGGER.error("failed to set wifi config ({})", esp_err_to_name(set_config_result));
        publish_event(wifi, WifiEvent::RadioStateOff);
        return;
    }

    esp_err_t wifi_start_result = esp_wifi_start();
    if (wifi_start_result != ESP_OK) {
        // TODO: disable radio, because radio state is in limbo between off and on
        wifi->setRadioState(RadioState::Off);
        LOGGER.error("failed to start wifi to begin connecting ({})", esp_err_to_name(wifi_start_result));
        publish_event(wifi, WifiEvent::RadioStateOff);
        return;
    }

    wifi->setRadioState(RadioState::On);
    publish_event(wifi, WifiEvent::Disconnected);
    LOGGER.info("Disconnected");
}

static bool shouldScanForAutoConnect(std::shared_ptr<Wifi> wifi) {
    auto lock = wifi->dataMutex.asScopedLock();
    if (!lock.lock(100)) {
        return false;
    }

    bool is_radio_in_scannable_state = wifi->getRadioState() == RadioState::On &&
       !wifi->isScanActive() &&
       !wifi->pause_auto_connect;

    if (!is_radio_in_scannable_state) {
        return false;
    }

    TickType_t current_time = kernel::getTicks();
    bool scan_time_has_looped = (current_time < wifi->last_scan_time);
    bool no_recent_scan = (current_time - wifi->last_scan_time) > (AUTO_SCAN_INTERVAL / portTICK_PERIOD_MS);

    if (!scan_time_has_looped && !no_recent_scan) {
    }

    return scan_time_has_looped || no_recent_scan;
}

void onAutoConnectTimer() {
    auto wifi = std::static_pointer_cast<Wifi>(wifi_singleton);
    // Automatic scanning is done so we can automatically connect to access points
    bool should_auto_scan = shouldScanForAutoConnect(wifi);
    if (should_auto_scan) {
        getMainDispatcher().dispatch([wifi]() { dispatchScan(wifi); });
    }
}

std::string getIp() {
    auto wifi = std::static_pointer_cast<Wifi>(wifi_singleton);

    auto lock = wifi->dataMutex.asScopedLock();
    lock.lock();

    return std::format("{}.{}.{}.{}", IP2STR(&wifi->ip_info.ip));
}

class WifiService final : public Service {

public:

    bool onStart(ServiceContext& service) override {
        assert(wifi_singleton == nullptr);
        wifi_singleton = std::make_shared<Wifi>();

        wifi_singleton->bootEventSubscription = kernel::subscribeSystemEvent(kernel::SystemEvent::BootSplash, [](auto) {
            bootSplashInit();
        });

        auto timer_interval = std::min(2000, AUTO_SCAN_INTERVAL);
        wifi_singleton->autoConnectTimer = std::make_unique<Timer>(Timer::Type::Periodic, timer_interval, [] { onAutoConnectTimer(); });
        // We want to try and scan more often in case of startup or scan lock failure
        wifi_singleton->autoConnectTimer->start();

        if (settings::shouldEnableOnBoot()) {
            LOGGER.info("Auto-enabling due to setting");
            getMainDispatcher().dispatch([] { dispatchEnable(wifi_singleton); });
        }

        return true;
    }

    void onStop(ServiceContext& service) override {
        auto wifi = wifi_singleton;
        assert(wifi != nullptr);

        RadioState state = wifi->getRadioState();
        if (state != RadioState::Off) {
            dispatchDisable(wifi);
        }

        wifi->autoConnectTimer->stop();
        wifi->autoConnectTimer = nullptr; // Must release as it holds a reference to this Wifi instance

        // Acquire all mutexes
        wifi->dataMutex.lock();
        wifi->radioMutex.lock();

        // Detach
        wifi_singleton = nullptr;

        // Release mutexes
        wifi->dataMutex.unlock();
        wifi->radioMutex.unlock();

        // Release (hopefully) last Wifi instance by scope
    }
};

extern const ServiceManifest manifest = {
    .id = "wifi",
    .createService = create<WifiService>
};

} // namespace

#endif // CONFIG_SOC_WIFI_SUPPORTED or CONFIG_SLAVE_SOC_WIFI_SUPPORTED
