#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/ChatProtocol.h>

#include <cstring>

namespace tt::app::chat {

size_t serializeMessage(const std::string& senderName, const std::string& target,
                        const std::string& message, Message& out) {
    if (senderName.size() >= SENDER_NAME_SIZE ||
        target.size() >= TARGET_SIZE ||
        message.size() >= MESSAGE_SIZE) {
        return 0;  // Signal truncation would occur
    }
    memset(&out, 0, sizeof(out));
    out.header = CHAT_MAGIC_HEADER;
    out.protocol_version = PROTOCOL_VERSION;
    strncpy(out.sender_name, senderName.c_str(), SENDER_NAME_SIZE - 1);
    strncpy(out.target, target.c_str(), TARGET_SIZE - 1);
    strncpy(out.message, message.c_str(), MESSAGE_SIZE - 1);

    // Return actual packet size: header + message length + null terminator
    return MESSAGE_HEADER_SIZE + message.size() + 1;
}

bool deserializeMessage(const uint8_t* data, int length, ParsedMessage& out) {
    // Accept variable-length packets (min header + 1 byte message, max full struct)
    if (length < static_cast<int>(MIN_PACKET_SIZE) || length > static_cast<int>(sizeof(Message))) {
        return false;
    }

    // Copy into aligned local struct to avoid unaligned access on embedded platforms
    Message msg;
    memset(&msg, 0, sizeof(msg));
    memcpy(&msg, data, length);

    if (msg.header != CHAT_MAGIC_HEADER) {
        return false;
    }

    if (msg.protocol_version != PROTOCOL_VERSION) {
        return false;
    }

    // Ensure null-termination for each field
    msg.sender_name[SENDER_NAME_SIZE - 1] = '\0';
    msg.target[TARGET_SIZE - 1] = '\0';

    // Calculate actual message length from packet size and ensure null termination
    size_t msgLen = length - MESSAGE_HEADER_SIZE;
    if (msgLen > 0 && msgLen < MESSAGE_SIZE) {
        msg.message[msgLen] = '\0';  // Handle malformed packets missing null terminator
    }
    msg.message[MESSAGE_SIZE - 1] = '\0';  // Safety: ensure buffer is always terminated

    out.senderName = msg.sender_name;
    out.target = msg.target;
    out.message = msg.message;

    return true;
}

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
