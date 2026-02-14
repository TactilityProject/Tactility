// SPDX-License-Identifier: Apache-2.0
/**
 * Time-keeping related functionality.
 * This includes functionality for both ticks and seconds.
 */
#pragma once

#ifndef __cplusplus
#include <assert.h>
#endif
#include <stdint.h>

#include "defines.h"
#include "tactility/freertos/task.h"

#ifdef ESP_PLATFORM
#include <esp_timer.h>
#else
#include <sys/time.h>
#include <time.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Projects that include this header must align with Tactility's frequency (e.g. apps)
#ifdef __cplusplus
static_assert(configTICK_RATE_HZ == 1000);
#else
static_assert(configTICK_RATE_HZ == 1000, "configTICK_RATE_HZ must be 1000");
#endif

static inline uint32_t get_tick_frequency() {
    return configTICK_RATE_HZ;
}

/** @return the amount of ticks that have passed since FreeRTOS' main task started */
static inline TickType_t get_ticks() {
    if (xPortInIsrContext() == pdTRUE) {
        return xTaskGetTickCountFromISR();
    } else {
        return xTaskGetTickCount();
    }
}

/** @return the milliseconds that have passed since FreeRTOS' main task started */
static inline size_t get_millis() {
    return get_ticks() * portTICK_PERIOD_MS;
}

static inline TickType_t get_timeout_remaining_ticks(TickType_t timeout, TickType_t start_time) {
    TickType_t ticks_passed = get_ticks() - start_time;
    if (ticks_passed >= timeout) {
        return 0;
    }
    return timeout - ticks_passed;
}

/** @return the frequency at which the kernel task schedulers operate */
uint32_t kernel_get_tick_frequency();

/** @return the microseconds that have passed since boot */
static inline int64_t get_micros_since_boot() {
#ifdef ESP_PLATFORM
    return esp_timer_get_time();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return ((int64_t)ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000);
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((int64_t)tv.tv_sec * 1000000LL) + tv.tv_usec;
#endif
}

/** Convert seconds to ticks */
static inline TickType_t seconds_to_ticks(uint32_t seconds) {
    return (uint64_t)seconds * 1000U / portTICK_PERIOD_MS;
}

/** Convert milliseconds to ticks */
static inline TickType_t millis_to_ticks(uint32_t milliSeconds) {
#if configTICK_RATE_HZ == 1000
    return (TickType_t)milliSeconds;
#else
    return static_cast<TickType_t>(((float)configTICK_RATE_HZ) / 1000.0f * (float)milliSeconds);
#endif
}

#ifdef __cplusplus
}
#endif
