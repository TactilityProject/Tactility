#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#ifdef CONFIG_ESP_WIFI_ENABLED

#include <Tactility/service/Service.h>
#include <Tactility/service/espnow/EspNow.h>

#include <Tactility/RecursiveMutex.h>

#include <functional>

namespace tt::service::espnow {

class EspNowService final : public Service {

    struct ReceiverSubscriptionData {
        ReceiverSubscription id;
        std::function<void(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length)> onReceive;
    };

    struct SendCallback {
        uint8_t macAddress[ESP_NOW_ETH_ALEN];
        bool success;
    };

    RecursiveMutex mutex;
    std::vector<ReceiverSubscriptionData> subscriptions;
    ReceiverSubscription lastSubscriptionId = 0;
    bool enabled = false;
    uint32_t espnowVersion = 0;

    // Dispatcher calls this and forwards to non-static function
    void enableFromDispatcher(const EspNowConfig& config);

    void disableFromDispatcher();

    static void receiveCallback(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length);
    void onReceive(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length);

public:

    // region Overrides

    bool onStart(ServiceContext& service) override;
    void onStop(ServiceContext& service) override;

    // endregion Overrides

    // region Internal API

    void enable(const EspNowConfig& config);

    void disable();

    bool isEnabled() const;

    bool addPeer(const esp_now_peer_info_t& peer);

    bool send(const uint8_t* address, const uint8_t* buffer, size_t bufferLength);

    ReceiverSubscription subscribeReceiver(std::function<void(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length)> onReceive);

    void unsubscribeReceiver(ReceiverSubscription subscription);

    uint32_t getVersion() const;

    // region Internal API
};

std::shared_ptr<EspNowService> findService();

}

#endif // ESP_PLATFORM
