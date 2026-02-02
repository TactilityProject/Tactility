// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Used for log output filtering */
enum LogLevel {
    LOG_LEVEL_ERROR, /*!< Critical errors, software module can not recover on its own */
    LOG_LEVEL_WARNING, /*!< Error conditions from which recovery measures have been taken */
    LOG_LEVEL_INFO, /*!< Information messages which describe normal flow of events */
    LOG_LEVEL_DEBUG, /*!< Extra information which is not necessary for normal use (values, pointers, sizes, etc). */
    LOG_LEVEL_VERBOSE /*!< Bigger chunks of debugging information, or frequent messages which can potentially flood the output. */
};

#ifndef ESP_PLATFORM

void log_generic(enum LogLevel level, const char* tag, const char* format, ...);

#define LOG_E(tag, ...) log_generic(LOG_LEVEL_ERROR, tag, ##__VA_ARGS__)
#define LOG_W(tag, ...) log_generic(LOG_LEVEL_WARNING, tag, ##__VA_ARGS__)
#define LOG_I(tag, ...) log_generic(LOG_LEVEL_INFO, tag, ##__VA_ARGS__)
#define LOG_D(tag, ...) log_generic(LOG_LEVEL_DEBUG, tag, ##__VA_ARGS__)
#define LOG_V(tag, ...) log_generic(LOG_LEVEL_VERBOSE, tag, ##__VA_ARGS__)

#else

#define LOG_E(tag, ...) ESP_LOGE(tag, ##__VA_ARGS__)
#define LOG_W(tag, ...) ESP_LOGW(tag, ##__VA_ARGS__)
#define LOG_I(tag, ...) ESP_LOGI(tag, ##__VA_ARGS__)
#define LOG_D(tag, ...) ESP_LOGD(tag, ##__VA_ARGS__)
#define LOG_V(tag, ...) ESP_LOGV(tag, ##__VA_ARGS__)

#endif

#ifdef __cplusplus
}
#endif
