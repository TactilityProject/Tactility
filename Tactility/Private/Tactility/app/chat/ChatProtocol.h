#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <cstddef>
#include <cstdint>
#include <string>

namespace tt::app::chat {

constexpr uint32_t CHAT_MAGIC_HEADER = 0x31544354; // "TCT1"
constexpr uint8_t PROTOCOL_VERSION = 0x01;
constexpr size_t SENDER_NAME_SIZE = 24;
constexpr size_t TARGET_SIZE = 24;
constexpr size_t MESSAGE_SIZE = 200;  //1417 Max for ESP-NOW v2.0 (1470 - 53 header bytes)

// Header size = offset to message field (header + version + sender_name + target)
constexpr size_t MESSAGE_HEADER_SIZE = 4 + 1 + SENDER_NAME_SIZE + TARGET_SIZE;  // 53 bytes
constexpr size_t MIN_PACKET_SIZE = MESSAGE_HEADER_SIZE + 1;  // At least 1 byte of message

struct __attribute__((packed)) Message {
    uint32_t header;
    uint8_t protocol_version;
    char sender_name[SENDER_NAME_SIZE];
    char target[TARGET_SIZE];   // empty=broadcast, "#channel" or "username"
    char message[MESSAGE_SIZE];
};

static_assert(sizeof(Message) == 1470, "Message struct must be exactly ESP-NOW v2.0 max payload");
static_assert(MESSAGE_HEADER_SIZE == offsetof(Message, message), "Header size calculation mismatch");

struct ParsedMessage {
    std::string senderName;
    std::string target;
    std::string message;
};

/** Serialize fields into the wire format.
 * Returns the actual packet size to send (variable length), or 0 on failure.
 * Short messages (< 250 bytes total) are compatible with ESP-NOW v1.0 devices. */
size_t serializeMessage(const std::string& senderName, const std::string& target,
                        const std::string& message, Message& out);

/** Deserialize a received buffer into a ParsedMessage.
 * Returns true if valid (correct magic, version, and minimum length). */
bool deserializeMessage(const uint8_t* data, int length, ParsedMessage& out);

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
