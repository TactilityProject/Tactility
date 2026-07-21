#include <Tactility/service/wifi/Wifi.h>

#include <Tactility/CoreDefines.h>
#include <Tactility/LogMessages.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/SystemEvents.h>
#include <Tactility/Tactility.h>
#include <Tactility/Timer.h>
#include <Tactility/service/Service.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/service/wifi/WifiBootSplashInit.h>
#include <Tactility/service/wifi/WifiGlobals.h>
#include <Tactility/service/wifi/WifiSettings.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/wifi.h>
#include <tactility/log.h>
#include <tactility/wifi_auto_scan.h>

#include <algorithm>
#include <atomic>

namespace tt::service::wifi {

constexpr auto* TAG = "WifiService";
constexpr auto AUTO_SCAN_INTERVAL = 10000; // ms

const char* radioStateToString(RadioState state) {
    switch (state) {
        using enum RadioState;
        case OnPending:
            return TT_STRINGIFY(OnPending);
        case On:
            return TT_STRINGIFY(On);
        case ConnectionPending:
            return TT_STRINGIFY(ConnectionPending);
        case ConnectionActive:
            return TT_STRINGIFY(ConnectionActive);
        case OffPending:
            return TT_STRINGIFY(OffPending);
        case Off:
            return TT_STRINGIFY(Off);
    }
    check(false, "not implemented");
}

extern const ServiceManifest manifest;

std::shared_ptr<ServiceContext> findServiceContext() {
    return findServiceContextById(manifest.id);
}

namespace {

// Everything below wraps a TactilityKernel WIFI_TYPE device: the driver owns
// the radio state, station state and scan results, this file only tracks the
// bits the kernel driver doesn't (in-flight connection target/credentials,
// auto-connect bookkeeping).

/** State lives for the entire process; only ever (re)initialized by onStart(). */
struct WifiServiceState {
    Device* device = nullptr;
    std::shared_ptr<PubSub<WifiEvent>> pubsub = std::make_shared<PubSub<WifiEvent>>();
    RecursiveMutex mutex;
    bool secureConnection = false;
    // Internal: set by connect()/disconnect() while a manual attempt is in flight, cleared on
    // connection success/failure. Distinct from externalScanPause below - the two must not
    // clobber each other, otherwise a caller's explicit pause (e.g. AutoScanPauseGuard during a
    // co-processor OTA) can be silently cleared by an unrelated connect/disconnect finishing.
    bool pauseAutoConnect = false;
    // External: only setAutoScanPaused() may set/clear this. Read alongside pauseAutoConnect to
    // gate scan scheduling (both must be false to scan).
    std::atomic<bool> externalScanPause{false};
    bool connectionTargetRemember = false;
    settings::WifiApSettings connectionTarget;
    uint16_t scanRecordLimit = TT_WIFI_SCAN_RECORD_LIMIT;
    TickType_t lastScanTime = kernel::MAX_TICKS;
    std::unique_ptr<Timer> autoConnectTimer;
    kernel::SystemEventSubscription bootEventSubscription = kernel::NoSystemEventSubscription;
};

WifiServiceState state;
bool started = false;

void onWifiDeviceEvent(Device* device, void* context, ::WifiEvent event);

// ---- Helpers ----

void publish(WifiEvent event) {
    state.pubsub->publish(event);
}

void publishRadioState(WifiRadioState radio_state) {
    WifiEvent event = {};
    event.type = WIFI_EVENT_TYPE_RADIO_STATE_CHANGED;
    event.radio_state = radio_state;
    publish(event);
}

RadioState combineRadioState(WifiRadioState radio, WifiStationState station) {
    switch (radio) {
        case WIFI_RADIO_STATE_OFF: return RadioState::Off;
        case WIFI_RADIO_STATE_ON_PENDING: return RadioState::OnPending;
        case WIFI_RADIO_STATE_OFF_PENDING: return RadioState::OffPending;
        case WIFI_RADIO_STATE_ON:
            switch (station) {
                case WIFI_STATION_STATE_CONNECTION_PENDING: return RadioState::ConnectionPending;
                case WIFI_STATION_STATE_CONNECTED: return RadioState::ConnectionActive;
                case WIFI_STATION_STATE_DISCONNECTED: default: return RadioState::On;
            }
    }
    return RadioState::Off;
}

// ---- Dispatched work (runs on the main task) ----

void dispatchSetEnabled(bool enabled) {
    LOG_I(TAG, "dispatchSetEnabled(%d)", (int)enabled);
    if (!started || state.device == nullptr) return;

    bool ready = device_is_ready(state.device);
    if (enabled == ready) {
        LOG_W(TAG, "Can't enable/disable from current state");
        return;
    }

    if (enabled) {
        publishRadioState(WIFI_RADIO_STATE_ON_PENDING);

        if (device_start(state.device) != ERROR_NONE) {
            LOG_E(TAG, "Failed to start WiFi device");
            publishRadioState(WIFI_RADIO_STATE_OFF);
            return;
        }

        if (wifi_add_event_callback(state.device, nullptr, onWifiDeviceEvent) != ERROR_NONE) {
            LOG_E(TAG, "Failed to register WiFi event callback");
            device_stop(state.device);
            publishRadioState(WIFI_RADIO_STATE_OFF);
            return;
        }

        state.pauseAutoConnect = false;
        state.lastScanTime = 0;
        publishRadioState(WIFI_RADIO_STATE_ON);
    } else {
        publishRadioState(WIFI_RADIO_STATE_OFF_PENDING);

        if (device_stop(state.device) != ERROR_NONE) {
            LOG_E(TAG, "Failed to stop WiFi device");
            publishRadioState(WIFI_RADIO_STATE_ON);
            return;
        }

        wifi_remove_event_callback(state.device, onWifiDeviceEvent);

        state.secureConnection = false;
        publishRadioState(WIFI_RADIO_STATE_OFF);
    }
}

void dispatchScan() {
    LOG_I(TAG, "dispatchScan()");
    if (!started || state.device == nullptr || !device_is_ready(state.device)) return;

    state.lastScanTime = kernel::getTicks();

    error_t result = wifi_scan(state.device);
    if (result != ERROR_NONE) {
        LOG_I(TAG, "Can't start scan (%s)", error_to_string(result));
    }
}

void dispatchConnect() {
    LOG_I(TAG, "dispatchConnect()");
    if (!started || state.device == nullptr) return;

    settings::WifiApSettings target;
    {
        auto lock = state.mutex.asScopedLock();
        if (!lock.lock(50 / portTICK_PERIOD_MS)) {
            LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "dispatchConnect()");
            return;
        }
        target = state.connectionTarget;
    }

    LOG_I(TAG, "Connecting to %s", target.ssid.c_str());

    error_t result = wifi_station_connect(state.device, target.ssid.c_str(), target.password.c_str(), target.channel);
    if (result != ERROR_NONE) {
        LOG_E(TAG, "Failed to connect to %s (%s)", target.ssid.c_str(), error_to_string(result));
        WifiEvent event = {};
        event.type = WIFI_EVENT_TYPE_STATION_CONNECTION_RESULT;
        // The driver couldn't even initiate the connection attempt; there's no
        // more specific WifiStationConnectionError for that.
        event.connection_error = WIFI_STATION_CONNECTION_ERROR_TIMEOUT;
        publish(event);
    }
    // On success, WIFI_EVENT_TYPE_STATION_STATE_CHANGED / _CONNECTION_RESULT arrive
    // asynchronously via onWifiDeviceEvent().
}

void dispatchDisconnect() {
    LOG_I(TAG, "dispatchDisconnect()");
    if (!started || state.device == nullptr) return;

    error_t result = wifi_station_disconnect(state.device);
    if (result != ERROR_NONE) {
        LOG_E(TAG, "Failed to disconnect (%s)", error_to_string(result));
    }
    // The Disconnected event arrives asynchronously via onWifiDeviceEvent().
}

bool findAutoConnectAp(settings::WifiApSettings& out) {
    for (const auto& record : getScanResults()) {
        if (settings::contains(record.ssid)) {
            settings::WifiApSettings loaded;
            if (settings::load(record.ssid, loaded)) {
                if (loaded.autoConnect) {
                    out = loaded;
                    return true;
                }
            } else {
                LOG_E(TAG, "Failed to load credentials for ssid %s", record.ssid);
            }
        }
    }
    return false;
}

void dispatchAutoConnect() {
    LOG_I(TAG, "dispatchAutoConnect()");
    if (state.pauseAutoConnect || state.externalScanPause.load()) {
        // A manual disconnect() or an in-progress manual connect() has paused
        // auto-connect, or a caller (e.g. AutoScanPauseGuard) has externally paused it.
        // This is called on every SCAN_FINISHED, not just the auto-connect timer's own
        // scans (e.g. WifiManage re-scans on show), so it must honor the pause instead of
        // reconnecting unconditionally.
        return;
    }
    RadioState radio_state = getRadioState();
    if (radio_state == RadioState::ConnectionActive || radio_state == RadioState::ConnectionPending) {
        // Already connected (or connecting): reconnecting to the same AP would just
        // force a pointless disconnect/reconnect blip, e.g. when WifiManage's
        // on-show scan finishes while we're already on the saved auto-connect AP.
        return;
    }
    settings::WifiApSettings target;
    if (findAutoConnectAp(target)) {
        LOG_I(TAG, "Auto-connecting to %s", target.ssid.c_str());
        connect(target, false);
        // connect() pauses auto-connect (it assumes a manual/user call); undo that
        // since this call was automatic.
        state.pauseAutoConnect = false;
    }
}

bool shouldScanForAutoConnect() {
    bool radio_scannable = getRadioState() == RadioState::On && !isScanning() &&
        !state.pauseAutoConnect && !state.externalScanPause.load();
    if (!radio_scannable) return false;

    TickType_t current_time = kernel::getTicks();
    bool scan_time_has_looped = current_time < state.lastScanTime;
    bool no_recent_scan = (current_time - state.lastScanTime) > (AUTO_SCAN_INTERVAL / portTICK_PERIOD_MS);
    return scan_time_has_looped || no_recent_scan;
}

void onAutoConnectTimer() {
    if (!started || state.device == nullptr) return;
    if (shouldScanForAutoConnect()) {
        getMainDispatcher().dispatch([] { dispatchScan(); });
    }
}

// ---- Kernel driver event bridge ----

void onWifiDeviceEvent(Device* /*device*/, void* /*context*/, ::WifiEvent event) {
    switch (event.type) {
        case WIFI_EVENT_TYPE_SCAN_FINISHED:
            getMainDispatcher().dispatch([] { dispatchAutoConnect(); });
            break;

        case WIFI_EVENT_TYPE_STATION_STATE_CHANGED:
            if (event.station_state == WIFI_STATION_STATE_DISCONNECTED) {
                // Don't touch pauseAutoConnect here: a deliberate disconnect() sets it
                // and relies on it staying set until a new connection is established.
                // Resetting it on every disconnect (including deliberate ones) would
                // let auto-connect immediately reconnect the user. Attempts that fail
                // while pending are unpaused via WIFI_EVENT_TYPE_STATION_CONNECTION_RESULT below.
                kernel::publishSystemEvent(kernel::SystemEvent::NetworkDisconnected);
            }
            break;

        case WIFI_EVENT_TYPE_STATION_CONNECTION_RESULT:
            if (event.connection_error == WIFI_STATION_CONNECTION_ERROR_NONE) {
                settings::WifiApSettings target;
                bool remember;
                {
                    auto lock = state.mutex.asScopedLock();
                    if (lock.lock(50 / portTICK_PERIOD_MS)) {
                        target = state.connectionTarget;
                        remember = state.connectionTargetRemember;
                        state.secureConnection = !target.password.empty();
                    } else {
                        remember = false;
                    }
                }
                {
                    auto lock = state.mutex.asScopedLock();
                    if (lock.lock(50 / portTICK_PERIOD_MS)) {
                        state.pauseAutoConnect = false;
                    }
                }
                LOG_I(TAG, "Connected to %s", target.ssid.c_str());
                if (remember && !settings::save(target)) {
                    LOG_E(TAG, "Failed to store credentials");
                }
                kernel::publishSystemEvent(kernel::SystemEvent::NetworkConnected);
            } else {
                // The pending connection attempt (which paused auto-connect via connect())
                // failed; unpause so auto-connect can try other saved APs.
                auto lock = state.mutex.asScopedLock();
                if (lock.lock(50 / portTICK_PERIOD_MS)) {
                    state.pauseAutoConnect = false;
                }
            }
            break;

        default:
            break;
    }

    // Forward the event as-is: subscribers inspect event.type and the
    // relevant union field directly, same as this function does.
    publish(event);
}

void autoScanSetPaused(bool paused) {
    LOG_I(TAG, "autoScanSetPaused(%d)", (int)paused);
    state.externalScanPause = paused;
}

} // namespace

// region Public functions

std::shared_ptr<PubSub<WifiEvent>> getPubsub() {
    return state.pubsub;
}

RadioState getRadioState() {
    if (!started || state.device == nullptr || !device_is_ready(state.device)) {
        return RadioState::Off;
    }

    WifiRadioState radio = WIFI_RADIO_STATE_OFF;
    WifiStationState station = WIFI_STATION_STATE_DISCONNECTED;
    wifi_get_radio_state(state.device, &radio);
    wifi_get_station_state(state.device, &station);
    return combineRadioState(radio, station);
}

std::string getConnectionTarget() {
    RadioState radio_state = getRadioState();
    if (radio_state != RadioState::ConnectionPending && radio_state != RadioState::ConnectionActive) {
        return "";
    }

    char ssid[33] = {};
    if (wifi_station_get_target_ssid(state.device, ssid) != ERROR_NONE) {
        return "";
    }
    return { ssid };
}

void scan() {
    LOG_I(TAG, "scan()");
    if (!started || state.device == nullptr) return;
    getMainDispatcher().dispatch([] { dispatchScan(); });
}

bool isScanning() {
    if (!started || state.device == nullptr) return false;
    return wifi_is_scanning(state.device);
}

void connect(const settings::WifiApSettings& ap, bool remember) {
    LOG_I(TAG, "connect(%s, %d)", ap.ssid.c_str(), (int)remember);
    if (!started || state.device == nullptr) return;

    bool radio_off;
    {
        auto lock = state.mutex.asScopedLock();
        if (!lock.lock(10 / portTICK_PERIOD_MS)) {
            LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED);
            return;
        }
        // Stop auto-connecting until the connection is established.
        state.pauseAutoConnect = true;
        state.connectionTarget = ap;
        state.connectionTargetRemember = remember;
        radio_off = !device_is_ready(state.device);
    }

    getMainDispatcher().dispatch([radio_off] {
        if (radio_off) {
            dispatchSetEnabled(true);
        }
        dispatchConnect();
    });
}

void disconnect() {
    LOG_I(TAG, "disconnect()");
    if (!started || state.device == nullptr) return;

    {
        auto lock = state.mutex.asScopedLock();
        if (!lock.lock(10 / portTICK_PERIOD_MS)) {
            LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED);
            return;
        }
        state.connectionTarget = settings::WifiApSettings("", "");
        // Manual disconnect (e.g. via app) should stop auto-connecting until a new connection is established.
        state.pauseAutoConnect = true;
    }

    getMainDispatcher().dispatch([] { dispatchDisconnect(); });
}

void setAutoScanPaused(bool paused) {
    autoScanSetPaused(paused);
}

void setScanRecords(uint16_t records) {
    LOG_I(TAG, "setScanRecords(%u)", records);
    if (!started) return;
    auto lock = state.mutex.asScopedLock();
    if (lock.lock(10 / portTICK_PERIOD_MS)) {
        state.scanRecordLimit = records;
    }
}

std::vector<WifiApRecord> getScanResults() {
    std::vector<WifiApRecord> records;
    if (!started || state.device == nullptr) return records;

    records.resize(state.scanRecordLimit);
    size_t count = records.size();
    if (wifi_get_scan_results(state.device, records.data(), &count) != ERROR_NONE) {
        records.clear();
        return records;
    }

    records.resize(count);
    return records;
}

void setEnabled(bool enabled) {
    LOG_I(TAG, "setEnabled(%d)", (int)enabled);
    if (!started || state.device == nullptr) return;
    getMainDispatcher().dispatch([enabled] { dispatchSetEnabled(enabled); });
}

bool isConnectionSecure() {
    return state.secureConnection;
}

int getRssi() {
    if (!started || state.device == nullptr) return 1;
    int32_t rssi = 0;
    if (wifi_station_get_rssi(state.device, &rssi) == ERROR_NONE) {
        return rssi;
    }
    return 1;
}

std::string getIp() {
    if (!started || state.device == nullptr) return "";
    char ipv4[16] = {};
    if (wifi_station_get_ipv4_address(state.device, ipv4) != ERROR_NONE) {
        return "";
    }
    return { ipv4 };
}

// endregion Public functions

namespace {

class WifiService final : public Service {

public:

    bool onStart(ServiceContext& /*service*/) override {
        check(!started);

        wifi_auto_scan_set_paused_function(autoScanSetPaused);

        state.device = wifi_find_first_registered_device();
        if (state.device == nullptr) {
            LOG_W(TAG, "No WiFi device found");
        }

        state.bootEventSubscription = kernel::subscribeSystemEvent(kernel::SystemEvent::BootSplash, [](auto) {
            bootSplashInit();
        });

        auto timer_interval = std::min(2000, AUTO_SCAN_INTERVAL);
        state.autoConnectTimer = std::make_unique<Timer>(Timer::Type::Periodic, timer_interval, [] { onAutoConnectTimer(); });
        // We want to try and scan more often in case of startup or scan lock failure.
        state.autoConnectTimer->start();

        started = true;
        return true;
    }

    void onStop(ServiceContext& /*service*/) override {
        check(started);
        started = false;

        state.autoConnectTimer->stop();
        state.autoConnectTimer = nullptr; // Must release as it holds a reference via its callback.

        kernel::unsubscribeSystemEvent(state.bootEventSubscription);
        state.bootEventSubscription = kernel::NoSystemEventSubscription;

        if (state.device != nullptr && device_is_ready(state.device)) {
            wifi_remove_event_callback(state.device, onWifiDeviceEvent);
            device_stop(state.device);
        }

        state.secureConnection = false;
        state.pauseAutoConnect = false;
        state.device = nullptr;

        wifi_auto_scan_set_paused_function(nullptr);
    }
};

} // namespace

extern const ServiceManifest manifest = {
    .id = "wifi",
    .createService = create<WifiService>
};

} // namespace tt::service::wifi
