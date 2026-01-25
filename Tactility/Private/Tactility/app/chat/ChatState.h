#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/RecursiveMutex.h>

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace tt::app::chat {

constexpr size_t MAX_MESSAGES = 100;

struct StoredMessage {
    std::string displayText;
    std::string target; // for channel filtering
    bool isOwn;
};

/** Thread safety: All public methods are mutex-protected.
 *  LVGL sync lock must be held separately when updating UI. */
class ChatState {

    mutable RecursiveMutex mutex;

    std::deque<StoredMessage> messages;
    std::string currentChannel = "#general";
    std::string localNickname = "Device";

public:
    ChatState() = default;
    ~ChatState() = default;

    ChatState(const ChatState&) = delete;
    ChatState& operator=(const ChatState&) = delete;
    ChatState(ChatState&&) = delete;
    ChatState& operator=(ChatState&&) = delete;

    void setLocalNickname(const std::string& nickname);
    std::string getLocalNickname() const;

    void setCurrentChannel(const std::string& channel);
    std::string getCurrentChannel() const;

    void addMessage(const StoredMessage& msg);

    /** Returns messages matching the current channel (or broadcast). */
    std::vector<StoredMessage> getFilteredMessages() const;
};

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
