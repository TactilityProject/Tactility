#pragma once

#include <stdint.h>

#include "Tactility/FreeRTOS/task.h"

#ifdef ESP_PLATFORM
#include <esp_timer.h>
#else
#include <sys/time.h>
#endif

// Projects that include this header must align with Tactility's frequency (e.g. apps)
static_assert(configTICK_RATE_HZ == 1000);

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

/** @return the frequency at which the kernel task schedulers operate */
uint32_t kernel_get_tick_frequency();

/** @return the microseconds that have passed since boot */
static inline int64_t get_micros_since_boot() {
#ifdef ESP_PLATFORM
    return esp_timer_get_time();
#else
    timeval tv;
    gettimeofday(&tv, nullptr);
    return 1000000 * tv.tv_sec + tv.tv_usec;
#endif
}

/** Convert seconds to ticks */
static inline TickType_t seconds_to_ticks(uint32_t seconds) {
    return static_cast<uint64_t>(seconds) * 1000U / portTICK_PERIOD_MS;
}

/** Convert milliseconds to ticks */
static inline TickType_t millis_to_ticks(uint32_t milliSeconds) {
#if configTICK_RATE_HZ == 1000
    return static_cast<TickType_t>(milliSeconds);
#else
    return static_cast<TickType_t>(((float)configTICK_RATE_HZ) / 1000.0f * (float)milliSeconds);
#endif
}
