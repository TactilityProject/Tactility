#pragma once

#include "LoggerAdapter.h"
#include "LoggerAdapterShared.h"

#include <cstdint>
#include <mutex>
#include <sstream>
#include <sys/time.h>

namespace tt {

static uint64_t getLogTimestamp() {
    static uint64_t base = 0U;
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        timeval time {};
        gettimeofday(&time, nullptr);
        base = ((uint64_t)time.tv_sec * 1000U) + (time.tv_usec / 1000U);
    });
    timeval time {};
    gettimeofday(&time, nullptr);
    uint64_t now = ((uint64_t)time.tv_sec * 1000U) + (time.tv_usec / 1000U);
    return now - base;
}

static const LoggerAdapter genericLoggerAdapter = [](LogLevel level, const char* tag, const char* message) {
    constexpr auto COLOR_RESET = "\033[0m";
    constexpr auto COLOR_GREY = "\033[37m";
    std::stringstream buffer;
    buffer << COLOR_GREY << getLogTimestamp() << ' ' << toTagColour(level) << toPrefix(level) << COLOR_GREY << " [" << COLOR_RESET << tag << COLOR_GREY << "] " << toMessageColour(level) << message << COLOR_RESET << std::endl;
    printf("%s", buffer.str().c_str());
};

}