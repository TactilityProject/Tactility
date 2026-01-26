#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <cstdint>
#include <cstring>
#include <esp_now.h>
#include <functional>

namespace tt::service::espnow {

typedef int ReceiverSubscription;
constexpr ReceiverSubscription NO_SUBSCRIPTION = -1;

// ESP-NOW version payload limits
constexpr size_t MAX_DATA_LEN_V1 = 250;   // ESP-NOW v1.0 max payload
constexpr size_t MAX_DATA_LEN_V2 = 1470;  // ESP-NOW v2.0 max payload (requires ESP-IDF v5.4+)

enum class Mode {
    Station,
    AccessPoint
};

struct EspNowConfig {
    uint8_t masterKey[ESP_NOW_KEY_LEN];
    uint8_t address[ESP_NOW_ETH_ALEN];
    Mode mode;
    uint8_t channel;
    bool longRange;
    bool encrypt;

    EspNowConfig(
        uint8_t masterKey[16],
        Mode mode,
        uint8_t channel,
        bool longRange,
        bool encrypt
    ) : mode(mode), channel(channel), longRange(longRange), encrypt(encrypt) {
        memcpy(this->masterKey, masterKey, 16);
    }
};

void enable(const EspNowConfig& config);

void disable();

bool isEnabled();

bool addPeer(const esp_now_peer_info_t& peer);

bool send(const uint8_t* address, const uint8_t* buffer, size_t bufferLength);

ReceiverSubscription subscribeReceiver(std::function<void(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length)> onReceive);

void unsubscribeReceiver(ReceiverSubscription subscription);

/** Get the ESP-NOW protocol version (1 for v1.0, 2 for v2.0). Returns 0 if service not running. */
uint32_t getVersion();

/** Get the maximum data length for current ESP-NOW version (250 for v1.0, 1470 for v2.0). Returns 0 if service not running. */
size_t getMaxDataLength();

}

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED