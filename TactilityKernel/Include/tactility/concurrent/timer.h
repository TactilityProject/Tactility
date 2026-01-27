// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "tactility/error.h"
#include "tactility/freertos/timers.h"
#include "tactility/concurrent/thread.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum TimerType {
    TIMER_TYPE_ONCE = 0,    // Timer triggers once after time has passed
    TIMER_TYPE_PERIODIC = 1 // Timer triggers repeatedly after time has passed
};

typedef void (*timer_callback_t)(void* context);
typedef void (*timer_pending_callback_t)(void* context, uint32_t arg);

struct Timer;

/**
 * @brief Creates a new timer instance.
 * @param[in] type The timer type.
 * @param[in] ticks The timer period in ticks.
 * @param[in] callback The callback function.
 * @param[in] context The context to pass to the callback function.
 * @return A pointer to the created timer instance, or NULL if allocation failed.
 */
struct Timer* timer_alloc(enum TimerType type, TickType_t ticks, timer_callback_t callback, void* context);

/**
 * @brief Destroys a timer instance.
 * @param[in] timer The timer instance to destroy.
 */
void timer_free(struct Timer* timer);

/**
 * @brief Starts the timer.
 * @param[in] timer The timer instance.
 * @return ERROR_NONE on success, ERROR_TIMEOUT if the command queue was full.
 */
error_t timer_start(struct Timer* timer);

/**
 * @brief Stops the timer.
 * @param[in] timer The timer instance.
 * @return ERROR_NONE on success, ERROR_TIMEOUT if the command queue was full.
 */
error_t timer_stop(struct Timer* timer);

/**
 * @brief Set a new interval and reset the timer.
 * @param[in] timer The timer instance.
 * @param[in] interval The new timer interval in ticks.
 * @return ERROR_NONE on success, ERROR_TIMEOUT if the command queue was full.
 */
error_t timer_reset_with_interval(struct Timer* timer, TickType_t interval);

/**
 * @brief Reset the timer.
 * @param[in] timer The timer instance.
 * @return ERROR_NONE on success, ERROR_TIMEOUT if the command queue was full.
 */
error_t timer_reset(struct Timer* timer);

/**
 * @brief Check if the timer is running.
 * @param[in] timer The timer instance.
 * @return true when the timer is running.
 */
bool timer_is_running(struct Timer* timer);

/**
 * @brief Gets the expiry time of the timer.
 * @param[in] timer The timer instance.
 * @return The expiry time in ticks.
 */
TickType_t timer_get_expiry_time(struct Timer* timer);

/**
 * @brief Calls xTimerPendFunctionCall internally.
 * @param[in] timer The timer instance.
 * @param[in] callback the function to call
 * @param[in] context the first function argument
 * @param[in] arg the second function argument
 * @param[in] timeout the function timeout (must set to 0 in ISR mode)
 * @return ERROR_NONE on success, ERROR_TIMEOUT if the command queue was full.
 */
error_t timer_set_pending_callback(struct Timer* timer, timer_pending_callback_t callback, void* context, uint32_t arg, TickType_t timeout);

/**
 * @brief Set callback priority (priority of the timer daemon task).
 * @param[in] timer The timer instance.
 * @param[in] priority The priority.
 */
void timer_set_callback_priority(struct Timer* timer, enum ThreadPriority priority);

#ifdef __cplusplus
}
#endif
