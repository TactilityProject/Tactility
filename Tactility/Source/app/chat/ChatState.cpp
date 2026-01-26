#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/ChatState.h>

namespace tt::app::chat {

void ChatState::setLocalNickname(const std::string& nickname) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    localNickname = nickname;
}

std::string ChatState::getLocalNickname() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return localNickname;
}

void ChatState::setCurrentChannel(const std::string& channel) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    currentChannel = channel;
}

std::string ChatState::getCurrentChannel() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return currentChannel;
}

void ChatState::addMessage(const StoredMessage& msg) {
    auto lock = mutex.asScopedLock();
    lock.lock();
    if (messages.size() >= MAX_MESSAGES) {
        messages.pop_front();
    }
    messages.push_back(msg);
}

std::vector<StoredMessage> ChatState::getFilteredMessages() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    std::vector<StoredMessage> result;
    result.reserve(messages.size()); // Avoid reallocations; may over-allocate slightly
    for (const auto& msg : messages) {
        // Show if broadcast (empty target) or matches current channel
        if (msg.target.empty() || msg.target == currentChannel) {
            result.push_back(msg);
        }
    }
    return result;
}

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
