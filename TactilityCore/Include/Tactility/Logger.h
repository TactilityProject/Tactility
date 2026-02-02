#pragma once

#include <tactility/log.h>

#include <format>

namespace tt {

class Logger {

    const char* tag;

    LogLevel level = LOG_LEVEL_INFO;

public:

    explicit Logger(const char* tag) : tag(tag) {}

    template <typename... Args>
    void verbose(std::format_string<Args...> format, Args&&... args) const {
        std::string message = std::format(format, std::forward<Args>(args)...);
        LOG_V(tag, "%s", message.c_str());
    }

    template <typename... Args>
    void debug(std::format_string<Args...> format, Args&&... args) const {
        std::string message = std::format(format, std::forward<Args>(args)...);
        LOG_D(tag, "%s", message.c_str());
    }

    template <typename... Args>
    void info(std::format_string<Args...> format, Args&&... args) const {
        std::string message = std::format(format, std::forward<Args>(args)...);
        LOG_I(tag, "%s", message.c_str());
    }

    template <typename... Args>
    void warn(std::format_string<Args...> format, Args&&... args) const {
        std::string message = std::format(format, std::forward<Args>(args)...);
        LOG_W(tag, "%s", message.c_str());
    }

    template <typename... Args>
    void error(std::format_string<Args...> format, Args&&... args) const {
        std::string message = std::format(format, std::forward<Args>(args)...);
        LOG_E(tag, "%s", message.c_str());
    }

    bool isLoggingVerbose() const {
        return LOG_LEVEL_VERBOSE <= level;
    }

    bool isLoggingDebug() const {
        return LOG_LEVEL_DEBUG <= level;
    }

    bool isLoggingInfo() const {
        return LOG_LEVEL_INFO <= level;
    }

    bool isLoggingWarning() const {
        return LOG_LEVEL_WARNING <= level;
    }

    bool isLoggingError() const {
        return LOG_LEVEL_ERROR <= level;
    }
};

}
