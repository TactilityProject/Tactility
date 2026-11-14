// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

#include <stdbool.h>
#include <stddef.h>

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

/**
 * Receives every formatted log line, including colour codes and the trailing newline.
 * Called on the log drain task with the listener registry locked, so it must be quick,
 * must not block, must not log and must not call log_subscribe() or log_unsubscribe().
 */
typedef void (*LogListener)(const char* line, size_t length, void* context);

/**
 * Registers a listener. Nothing is stored while there are no listeners.
 * @retval false when all listener slots are taken or the log queue isn't initialized
 */
bool log_subscribe(LogListener listener, void* context);

/** Removes a listener. When this returns, the listener is not running and won't be called again. */
void log_unsubscribe(LogListener listener, void* context);

#ifndef ESP_PLATFORM

void log_generic(enum LogLevel level, const char* tag, const char* format, ...);

/**
 * Makes logging on the calling thread write directly to the console.
 * Must be called by threads that are not FreeRTOS tasks before they log, because the log queue can only be used from tasks.
 */
void log_set_direct_write_for_current_thread(void);

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
