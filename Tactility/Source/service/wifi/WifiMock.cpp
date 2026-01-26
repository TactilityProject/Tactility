#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if not defined(CONFIG_SOC_WIFI_SUPPORTED) && not defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/service/wifi/Wifi.h>

#include <Tactility/PubSub.h>
#include <Tactility/Check.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/service/Service.h>
#include <Tactility/service/ServiceManifest.h>

namespace tt::service::wifi {

struct Wifi {
    /** @brief Locking mechanism for modifying the Wifi instance */
    RecursiveMutex mutex;
    /** @brief The public event bus */
    std::shared_ptr<PubSub<WifiEvent>> pubsub = std::make_shared<PubSub<WifiEvent>>();
    /** @brief The internal message queue */
    bool scan_active = false;
    bool secure_connection = false;
    RadioState radio_state = RadioState::ConnectionActive;
};


static Wifi* wifi = nullptr;

// region Static

static void publish_event(WifiEvent event) {
    wifi->pubsub->publish(event);
}

// endregion Static

// region Public functions

std::shared_ptr<PubSub<WifiEvent>> getPubsub() {
    assert(wifi);
    return wifi->pubsub;
}

RadioState getRadioState() {
    return wifi->radio_state;
}

std::string getConnectionTarget() {
    return "Home Wifi";
}

void scan() {
    assert(wifi);
    wifi->scan_active = false; // TODO: enable and then later disable automatically
}

bool isScanning() {
    assert(wifi);
    return wifi->scan_active;
}

void connect(const settings::WifiApSettings& ap, bool remember) {
    assert(wifi);
    // TODO: implement
}

void disconnect() {
    assert(wifi);
}

void setScanRecords(uint16_t records) {
    assert(wifi);
    // TODO: implement
}

std::vector<ApRecord> getScanResults() {
    check(wifi);

    std::vector<ApRecord> records;
    records.push_back((ApRecord) {
        .ssid = "Home Wifi",
        .rssi = -30,
        .channel = 0,
        .auth_mode = WIFI_AUTH_WPA2_PSK
    });
    records.push_back((ApRecord) {
        .ssid = "No place like 127.0.0.1",
        .rssi = -67,
        .channel = 0,
        .auth_mode = WIFI_AUTH_WPA2_PSK
    });
    records.push_back((ApRecord) {
        .ssid = "Pretty fly for a Wi-Fi",
        .rssi = -70,
        .channel = 0,
        .auth_mode = WIFI_AUTH_WPA2_PSK
    });
    records.push_back((ApRecord) {
        .ssid = "An AP with a really, really long name",
        .rssi = -80,
        .channel = 0,
        .auth_mode = WIFI_AUTH_WPA2_PSK
    });
    records.push_back((ApRecord) {
        .ssid = "Bad Reception",
        .rssi = -90,
        .channel = 0,
        .auth_mode = WIFI_AUTH_OPEN
    });

    return records;
}

void setEnabled(bool enabled) {
    assert(wifi != nullptr);
    if (enabled) {
        wifi->radio_state = RadioState::On;
        wifi->secure_connection = true;
    } else {
        wifi->radio_state = RadioState::Off;
    }
}

bool isConnectionSecure() {
    return wifi->secure_connection;
}

int getRssi() {
    if (wifi->radio_state == RadioState::ConnectionActive) {
        return -30;
    } else {
        return 0;
    }
}

std::string getIp() {
    return "192.168.1.2";
}

// endregion Public functions

class WifiService final : public Service {

public:

    bool onStart(ServiceContext& service) override {
        check(wifi == nullptr);
        wifi = new Wifi();
        return true;
    }

    void onStop(ServiceContext& service) override {
        check(wifi != nullptr);
        delete wifi;
        wifi = nullptr;
    }
};

extern const ServiceManifest manifest = {
    .id = "wifi",
    .createService = create<WifiService>
};

} // namespace

#endif // ESP_PLATFORM