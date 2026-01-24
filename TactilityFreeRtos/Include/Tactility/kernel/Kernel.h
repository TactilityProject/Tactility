// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../freertoscompat/PortCompat.h"
#include "../freertoscompat/Task.h"

#ifdef ESP_PLATFORM
#include <esp_timer.h>
#include <rom/ets_sys.h>
#else
#include <sys/time.h>
#include <cstdint>
#include <unistd.h>
#endif

#include <cassert>

namespace tt::kernel {

constexpr TickType_t MAX_TICKS = ~static_cast<TickType_t>(0);

/** @return the frequency at which the kernel task schedulers operate */
constexpr uint32_t getTickFrequency() {
    return configTICK_RATE_HZ;
}

/** @return the amount of ticks that has passed in the main kernel task */
inline TickType_t getTicks() {
    if (xPortInIsrContext() == pdTRUE) {
        return xTaskGetTickCountFromISR();
    } else {
        return xTaskGetTickCount();
    }
}


/** @return the amount of milliseconds that has passed in the main kernel tasks */
inline size_t getMillis() {
    return getTicks() * portTICK_PERIOD_MS;
}

/** @return the microseconds that have passed since boot */
inline int64_t getMicrosSinceBoot() {
#ifdef ESP_PLATFORM
    return esp_timer_get_time();
#else
    timeval tv;
    gettimeofday(&tv, nullptr);
    return 1000000 * tv.tv_sec + tv.tv_usec;
#endif
}

/** Convert seconds to ticks */
inline TickType_t secondsToTicks(uint32_t seconds) {
    return static_cast<uint64_t>(seconds) * 1000U / portTICK_PERIOD_MS;
}

/** Convert milliseconds to ticks */
inline TickType_t millisToTicks(uint32_t milliSeconds) {
#if configTICK_RATE_HZ == 1000
    return static_cast<TickType_t>(milliSeconds);
#else
    return static_cast<TickType_t>(((float)configTICK_RATE_HZ) / 1000.0f * (float)milliSeconds);
#endif
}

/**
 * Delay the current task for the specified amount of ticks
 * @warning Does not work in ISR context
 */
inline void delayTicks(TickType_t ticks) {
    assert(xPortInIsrContext() == pdFALSE);
    if (ticks == 0U) {
        taskYIELD();
    } else {
        vTaskDelay(ticks);
    }
}

/**
 * Delay the current task for the specified amount of milliseconds
 * @warning Does not work in ISR context
 */
inline void delayMillis(uint32_t milliSeconds) {
    delayTicks(millisToTicks(milliSeconds));
}

/**
 * Stall the currently active CPU core for the specified amount of microseconds.
 * This does not allow other tasks to run on the stalled CPU core.
 */
inline void delayMicros(uint32_t microseconds) {
#ifdef ESP_PLATFORM
    ets_delay_us(microseconds);
#else
    usleep(microseconds);
#endif
}

} // namespace
