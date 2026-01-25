#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/service/espnow/EspNow.h>
#include <Tactility/service/espnow/EspNowService.h>

#include <Tactility/Logger.h>

namespace tt::service::espnow {

static const auto LOGGER = Logger("EspNow");

void enable(const EspNowConfig& config) {
    auto service = findService();
    if (service != nullptr) {
        service->enable(config);
    } else {
        LOGGER.error("Service not found");
    }
}

void disable() {
    auto service = findService();
    if (service != nullptr) {
        service->disable();
    } else {
        LOGGER.error("Service not found");
    }
}

bool isEnabled() {
    auto service = findService();
    if (service != nullptr) {
        return service->isEnabled();
    } else {
        LOGGER.error("Service not found");
        return false;
    }
}

bool addPeer(const esp_now_peer_info_t& peer) {
    auto service = findService();
    if (service != nullptr) {
        return service->addPeer(peer);
    } else {
        LOGGER.error("Service not found");
        return false;
    }
}

bool send(const uint8_t* address, const uint8_t* buffer, size_t bufferLength) {
    auto service = findService();
    if (service != nullptr) {
        return service->send(address, buffer, bufferLength);
    } else {
        LOGGER.error("Service not found");
        return false;
    }
}

ReceiverSubscription subscribeReceiver(std::function<void(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length)> onReceive) {
    auto service = findService();
    if (service != nullptr) {
        return service->subscribeReceiver(onReceive);
    } else {
        LOGGER.error("Service not found");
        return -1;
    }
}

void unsubscribeReceiver(ReceiverSubscription subscription) {
    auto service = findService();
    if (service != nullptr) {
        service->unsubscribeReceiver(subscription);
    } else {
        LOGGER.error("Service not found");
    }
}

uint32_t getVersion() {
    auto service = findService();
    if (service != nullptr) {
        return service->getVersion();
    }
    LOGGER.error("Service not found");
    return 0;
}

size_t getMaxDataLength() {
    auto v = getVersion();
    if (v == 0) return 0;
    return v >= 2 ? MAX_DATA_LEN_V2 : MAX_DATA_LEN_V1;
}

}

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
