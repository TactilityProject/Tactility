#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tt::app::chat {

// Protocol identification
constexpr uint32_t CHAT_MAGIC_V2 = 0x54435432; // "TCT2"
constexpr uint16_t PROTOCOL_VERSION = 2;

// Broadcast/channel target ID
constexpr uint32_t BROADCAST_ID = 0;

// Payload types
enum class PayloadType : uint8_t {
    TextMessage = 1,
    // Future: Position = 2, Telemetry = 3, etc.
};

// Wire format header (16 bytes)
struct __attribute__((packed)) MessageHeader {
    uint32_t magic;             // CHAT_MAGIC_V2
    uint16_t protocol_version;  // PROTOCOL_VERSION
    uint32_t from;              // Sender ID (random, stored in settings)
    uint32_t to;                // Recipient ID (0 = broadcast/channel)
    uint8_t payload_type;       // PayloadType enum
    uint8_t payload_size;       // Size of payload following header
};

static_assert(sizeof(MessageHeader) == 16, "MessageHeader must be 16 bytes");

// Size limits
constexpr size_t HEADER_SIZE = sizeof(MessageHeader);
constexpr size_t MAX_PAYLOAD_V1 = 250 - HEADER_SIZE;   // 234 bytes for ESP-NOW v1
constexpr size_t MAX_PAYLOAD_V2 = 1470 - HEADER_SIZE;  // 1454 bytes for ESP-NOW v2
constexpr size_t MAX_NICKNAME_LEN = 23;   // Max nickname length (excluding null)
constexpr size_t MAX_TARGET_LEN = 23;     // Max target/channel length (excluding null)

// Max message length: 255 (uint8_t payload_size) - nickname - null - target - null
// Using max lengths: 255 - 23 - 1 - 23 - 1 = 207, rounded down for safety
constexpr size_t MAX_MESSAGE_LEN = 200;

// Parsed message for application use
struct ParsedMessage {
    uint32_t senderId;
    uint32_t targetId;
    std::string senderName;
    std::string target;
    std::string message;
};

/** Serialize a text message into wire format.
 * @param senderId Sender's unique ID
 * @param targetId Recipient ID (0 for broadcast/channel)
 * @param senderName Sender's display name
 * @param target Channel name (e.g. "#general") or empty for broadcast
 * @param message The message text
 * @param out Output buffer (will be resized to fit)
 * @return true on success, false if inputs exceed limits
 */
bool serializeTextMessage(uint32_t senderId, uint32_t targetId,
                          const std::string& senderName, const std::string& target,
                          const std::string& message, std::vector<uint8_t>& out);

/** Deserialize a received buffer into a ParsedMessage.
 * @param data Raw received data
 * @param length Length of received data
 * @param out Parsed message output
 * @return true if valid (correct magic, version, and format)
 */
bool deserializeMessage(const uint8_t* data, size_t length, ParsedMessage& out);

/** Get maximum message length for current ESP-NOW version.
 * Accounts for header + nickname + target overhead. */
size_t getMaxMessageLength(size_t nicknameLen, size_t targetLen);

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
