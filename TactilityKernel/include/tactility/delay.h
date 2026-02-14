#pragma once

#include "time.h"
#include <stdint.h>

#ifdef ESP_PLATFORM
#include <rom/ets_sys.h>
#else
#include <unistd.h>
#endif

#include <tactility/freertos/freertos.h>
#include <tactility/check.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Delay the current task for the specified amount of ticks
 * @warning Does not work in ISR context
 */
static inline void delay_ticks(TickType_t ticks) {
    check(xPortInIsrContext() == pdFALSE);
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
static inline void delay_millis(uint32_t milliSeconds) {
    delay_ticks(millis_to_ticks(milliSeconds));
}

/**
 * Stall the currently active CPU core for the specified amount of microseconds.
 * This does not allow other tasks to run on the stalled CPU core.
 */
static inline void delay_micros(uint32_t microseconds) {
#ifdef ESP_PLATFORM
    ets_delay_us(microseconds);
#else
    usleep(microseconds);
#endif
}

#ifdef __cplusplus
}
#endif
