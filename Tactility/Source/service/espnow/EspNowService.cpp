#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/Logger.h>
#include <Tactility/Tactility.h>
#include <Tactility/service/espnow/EspNowService.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/service/espnow/EspNowWifi.h>

#include <cstring>
#include <esp_now.h>
#include <esp_random.h>

namespace tt::service::espnow {

extern const ServiceManifest manifest;

static const auto LOGGER = Logger("EspNowService");
static uint8_t BROADCAST_MAC[ESP_NOW_ETH_ALEN];

constexpr TickType_t MAX_DELAY = 1000U / portTICK_PERIOD_MS;
constexpr bool isBroadcastAddress(uint8_t address[ESP_NOW_ETH_ALEN]) { return memcmp(address, BROADCAST_MAC, ESP_NOW_ETH_ALEN) == 0; }

bool EspNowService::onStart(ServiceContext& service) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    memset(BROADCAST_MAC, 0xFF, sizeof(BROADCAST_MAC));

    return true;
}

void EspNowService::onStop(ServiceContext& service) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (isEnabled()) {
        disable();
    }
}

// region Enable

void EspNowService::enable(const EspNowConfig& config) {
    getMainDispatcher().dispatch([this, config] {
        enableFromDispatcher(config);
    });
}

void EspNowService::enableFromDispatcher(const EspNowConfig& config) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (enabled) {
        return;
    }

    if (!initWifi(config)) {
        LOGGER.error("initWifi() failed");
        return;
    }

    if (esp_now_init() != ESP_OK) {
        LOGGER.error("esp_now_init() failed");
        return;
    }

    if (esp_now_register_recv_cb(receiveCallback) != ESP_OK) {
        LOGGER.error("esp_now_register_recv_cb() failed");
        return;
    }

//#if CONFIG_ESPNOW_ENABLE_POWER_SAVE
//    ESP_ERROR_CHECK( esp_now_set_wake_window(CONFIG_ESPNOW_WAKE_WINDOW) );
//    ESP_ERROR_CHECK( esp_wifi_connectionless_module_set_wake_interval(CONFIG_ESPNOW_WAKE_INTERVAL) );
//#endif

    if (esp_now_set_pmk(config.masterKey) != ESP_OK) {
        LOGGER.error("esp_now_set_pmk() failed");
        return;
    }

    // Add default unencrypted broadcast peer
    esp_now_peer_info_t broadcast_peer;
    memset(&broadcast_peer, 0, sizeof(esp_now_peer_info_t));
    memcpy(broadcast_peer.peer_addr, BROADCAST_MAC, sizeof(BROADCAST_MAC));
    service::espnow::addPeer(broadcast_peer);

    enabled = true;
}

// endregion Enable

// region Disable

void EspNowService::disable() {
    getMainDispatcher().dispatch([this]() {
        disableFromDispatcher();
    });
}

void EspNowService::disableFromDispatcher() {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!enabled) {
        return;
    }

    if (esp_now_deinit() != ESP_OK) {
        LOGGER.error("esp_now_deinit() failed");
    }

    if (!deinitWifi()) {
        LOGGER.error("deinitWifi() failed");
    }

    enabled = false;
}

// region Disable

// region Callbacks

void EspNowService::receiveCallback(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length) {
    auto service = findService();
    if (service == nullptr) {
        LOGGER.error("Service not running");
        return;
    }
    service->onReceive(receiveInfo, data, length);
}

void EspNowService::onReceive(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    LOGGER.debug("Received {} bytes", length);

    for (const auto& item: subscriptions) {
        item.onReceive(receiveInfo, data, length);
    }
}

// endregion Callbacks

bool EspNowService::isEnabled() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return enabled;
}

bool EspNowService::addPeer(const esp_now_peer_info_t& peer) {
    if (esp_now_add_peer(&peer) != ESP_OK) {
        LOGGER.error("Failed to add peer");
        return false;
    } else {
        LOGGER.info("Peer added");
        return true;
    }
}

bool EspNowService::send(const uint8_t* address, const uint8_t* buffer, size_t bufferLength) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    if (!isEnabled()) {
        return false;
    } else {
        return esp_now_send(address, buffer, bufferLength) == ESP_OK;
    }
}

ReceiverSubscription EspNowService::subscribeReceiver(std::function<void(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length)> onReceive) {
    auto lock = mutex.asScopedLock();
    lock.lock();

    auto id = lastSubscriptionId++;

    subscriptions.push_back(ReceiverSubscriptionData {
        .id = id,
        .onReceive = onReceive
    });

    return id;
}

void EspNowService::unsubscribeReceiver(ReceiverSubscription subscriptionId) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    std::erase_if(subscriptions, [subscriptionId](auto& subscription) { return subscription.id == subscriptionId; });
}

std::shared_ptr<EspNowService> findService() {
    return std::static_pointer_cast<EspNowService>(
        findServiceById(manifest.id)
    );
}

extern const ServiceManifest manifest = {
    .id = "EspNow",
    .createService = create<EspNowService>
};

}

#endif // ESP_PLATFORM
