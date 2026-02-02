// SPDX-License-Identifier: Apache-2.0

#ifndef ESP_PLATFORM

#include <tactility/log.h>

#include <mutex>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/time.h>

static const char* get_log_color(LogLevel level) {
    using enum LogLevel;
    switch (level) {
        case LOG_LEVEL_ERROR:
            return "\033[1;31m";
        case LOG_LEVEL_WARNING:
            return "\033[1;33m";
        case LOG_LEVEL_INFO:
            return "\033[32m";
        case LOG_LEVEL_DEBUG:
            return "\033[36m";
        case LOG_LEVEL_VERBOSE:
            return "\033[37m";
        default:
            return "";
    }
}

static inline char get_log_prefix(LogLevel level) {
    using enum LogLevel;
    switch (level) {
        case LOG_LEVEL_ERROR:
            return 'E';
        case LOG_LEVEL_WARNING:
            return 'W';
        case LOG_LEVEL_INFO:
            return 'I';
        case LOG_LEVEL_DEBUG:
            return 'D';
        case LOG_LEVEL_VERBOSE:
            return 'V';
        default:
            return '?';
    }
}

static uint64_t get_log_timestamp() {
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

extern "C" {

void log_generic(enum LogLevel level, const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    printf("%s %c (%" PRIu64 ") \033[37m%s\033[0m ", get_log_color(level), get_log_prefix(level), get_log_timestamp(), tag);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

}

#endif