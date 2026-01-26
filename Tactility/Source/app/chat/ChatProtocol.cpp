#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/ChatProtocol.h>
#include <Tactility/service/espnow/EspNow.h>

#include <algorithm>
#include <cstring>

namespace tt::app::chat {

bool serializeTextMessage(uint32_t senderId, uint32_t targetId,
                          const std::string& senderName, const std::string& target,
                          const std::string& message, std::vector<uint8_t>& out) {
    // Validate input lengths
    if (senderName.size() > MAX_NICKNAME_LEN || target.size() > MAX_TARGET_LEN) {
        return false;
    }

    // Calculate payload size: nickname + null + target + null + message
    size_t payloadSize = senderName.size() + 1 + target.size() + 1 + message.size();

    // Check against ESP-NOW limits (guard against underflow if getMaxDataLength < HEADER_SIZE)
    size_t maxData = service::espnow::getMaxDataLength();
    if (maxData <= HEADER_SIZE) {
        return false;
    }
    size_t maxPayload = maxData - HEADER_SIZE;
    if (payloadSize > maxPayload || payloadSize > 255) {
        return false;  // payload_size is uint8_t
    }

    // Allocate output buffer
    out.resize(HEADER_SIZE + payloadSize);

    // Build header
    MessageHeader header;
    header.magic = CHAT_MAGIC_V2;
    header.protocol_version = PROTOCOL_VERSION;
    header.from = senderId;
    header.to = targetId;
    header.payload_type = static_cast<uint8_t>(PayloadType::TextMessage);
    header.payload_size = static_cast<uint8_t>(payloadSize);

    // Copy header to output
    memcpy(out.data(), &header, HEADER_SIZE);

    // Build payload: nickname\0 + target\0 + message
    uint8_t* payload = out.data() + HEADER_SIZE;
    size_t offset = 0;

    memcpy(payload + offset, senderName.c_str(), senderName.size() + 1);
    offset += senderName.size() + 1;

    memcpy(payload + offset, target.c_str(), target.size() + 1);
    offset += target.size() + 1;

    memcpy(payload + offset, message.c_str(), message.size());
    // Note: message is NOT null-terminated in wire format (length is implicit)

    return true;
}

bool deserializeMessage(const uint8_t* data, size_t length, ParsedMessage& out) {
    // Minimum: header + at least 2 null terminators (empty nickname + empty target)
    if (length < HEADER_SIZE + 2) {
        return false;
    }

    // Copy header to aligned struct
    MessageHeader header;
    memcpy(&header, data, HEADER_SIZE);

    // Validate header
    if (header.magic != CHAT_MAGIC_V2) {
        return false;
    }

    if (header.protocol_version != PROTOCOL_VERSION) {
        return false;
    }

    // Validate payload size
    if (header.payload_size != length - HEADER_SIZE) {
        return false;
    }

    // Only handle text messages for now
    if (header.payload_type != static_cast<uint8_t>(PayloadType::TextMessage)) {
        return false;
    }

    // Parse payload
    const uint8_t* payload = data + HEADER_SIZE;
    size_t payloadLen = header.payload_size;

    // Find nickname (null-terminated)
    const char* nicknameStart = reinterpret_cast<const char*>(payload);
    size_t nicknameLen = strnlen(nicknameStart, payloadLen);
    if (nicknameLen >= payloadLen) {
        return false;  // No null terminator found
    }

    size_t offset = nicknameLen + 1;
    size_t remaining = payloadLen - offset;

    // Find target (null-terminated)
    const char* targetStart = reinterpret_cast<const char*>(payload + offset);
    size_t targetLen = strnlen(targetStart, remaining);
    if (targetLen >= remaining) {
        return false;  // No null terminator found
    }

    offset += targetLen + 1;
    remaining = payloadLen - offset;

    // Rest is the message (not null-terminated)
    const char* messageStart = reinterpret_cast<const char*>(payload + offset);

    // Populate output
    out.senderId = header.from;
    out.targetId = header.to;
    out.senderName = std::string(nicknameStart, nicknameLen);
    out.target = std::string(targetStart, targetLen);
    out.message = std::string(messageStart, remaining);

    return true;
}

size_t getMaxMessageLength(size_t nicknameLen, size_t targetLen) {
    // Guard against underflow if getMaxDataLength < HEADER_SIZE
    size_t maxData = service::espnow::getMaxDataLength();
    if (maxData <= HEADER_SIZE) {
        return 0;
    }
    size_t maxPayload = maxData - HEADER_SIZE;

    // Payload: nickname + null + target + null + message
    size_t overhead = nicknameLen + 1 + targetLen + 1;
    if (overhead >= maxPayload || overhead > 255) {
        return 0;
    }
    // Cap at 255 since payload_size is uint8_t
    size_t maxFromEspNow = maxPayload - overhead;
    size_t maxFromPayloadSize = 255 - overhead;
    return std::min(maxFromEspNow, maxFromPayloadSize);
}

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
