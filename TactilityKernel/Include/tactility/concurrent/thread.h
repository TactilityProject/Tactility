// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "tactility/error.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

#include <tactility/freertos/task.h>
#include <tactility/concurrent/mutex.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    THREAD_STATE_STOPPED,
    THREAD_STATE_STARTING,
    THREAD_STATE_RUNNING,
} ThreadState;

/** ThreadPriority */
enum ThreadPriority {
    THREAD_PRIORITY_NONE = 0U,
    THREAD_PRIORITY_IDLE = 1U,
    THREAD_PRIORITY_LOWER = 2U,
    THREAD_PRIORITY_LOW = 3U,
    THREAD_PRIORITY_NORMAL = 4U,
    THREAD_PRIORITY_HIGH = 5U,
    THREAD_PRIORITY_HIGHER = 6U,
    THREAD_PRIORITY_CRITICAL = 7U
};

typedef int32_t (*thread_main_fn_t)(void* context);
typedef void (*thread_state_callback_t)(ThreadState state, void* context);

struct Thread;
typedef struct Thread Thread;

/**
 * @brief Creates a new thread instance with default settings.
 * @return A pointer to the created Thread instance, or NULL if allocation failed.
 */
Thread* thread_alloc(void);

/**
 * @brief Creates a new thread instance with specified parameters.
 * @param[in] name The name of the thread.
 * @param[in] stack_size The size of the thread stack in bytes.
 * @param[in] function The main function to be executed by the thread.
 * @param[in] function_context A pointer to the context to be passed to the main function.
 * @param[in] affinity The CPU core affinity for the thread (e.g., tskNO_AFFINITY).
 * @return A pointer to the created Thread instance, or NULL if allocation failed.
 */
Thread* thread_alloc_full(
    const char* name,
    configSTACK_DEPTH_TYPE stack_size,
    thread_main_fn_t function,
    void* function_context,
    portBASE_TYPE affinity
);

/**
 * @brief Destroys a thread instance.
 * @param[in] thread The thread instance to destroy.
 * @note The thread must be in the STOPPED state.
 */
void thread_free(Thread* thread);

/**
 * @brief Sets the name of the thread.
 * @param[in] thread The thread instance.
 * @param[in] name The new name for the thread.
 * @note Can only be called when the thread is in the STOPPED state.
 */
void thread_set_name(Thread* thread, const char* name);

/**
 * @brief Sets the stack size for the thread.
 * @param[in] thread The thread instance.
 * @param[in] stack_size The stack size in bytes. Must be a multiple of 4.
 * @note Can only be called when the thread is in the STOPPED state.
 */
void thread_set_stack_size(Thread* thread, size_t stack_size);

/**
 * @brief Sets the CPU core affinity for the thread.
 * @param[in] thread The thread instance.
 * @param[in] affinity The CPU core affinity.
 * @note Can only be called when the thread is in the STOPPED state.
 */
void thread_set_affinity(Thread* thread, portBASE_TYPE affinity);

/**
 * @brief Sets the main function and context for the thread.
 * @param[in] thread The thread instance.
 * @param[in] function The main function to be executed.
 * @param[in] context A pointer to the context to be passed to the main function.
 * @note Can only be called when the thread is in the STOPPED state.
 */
void thread_set_main_function(Thread* thread, thread_main_fn_t function, void* context);

/**
 * @brief Sets the priority for the thread.
 * @param[in] thread The thread instance.
 * @param[in] priority The thread priority.
 * @note Can only be called when the thread is in the STOPPED state.
 */
void thread_set_priority(Thread* thread, enum ThreadPriority priority);

/**
 * @brief Sets a callback to be invoked when the thread state changes.
 * @param[in] thread The thread instance.
 * @param[in] callback The callback function.
 * @param[in] context A pointer to the context to be passed to the callback function.
 * @note Can only be called when the thread is in the STOPPED state.
 */
void thread_set_state_callback(Thread* thread, thread_state_callback_t callback, void* context);

/**
 * @brief Gets the current state of the thread.
 * @param[in] thread The thread instance.
 * @return The current ThreadState.
 */
ThreadState thread_get_state(Thread* thread);

/**
 * @brief Starts the thread execution.
 * @param[in] thread The thread instance.
 * @note The thread must be in the STOPPED state and have a main function set.
 * @retval ERROR_NONE when the thread was started
 * @retval ERROR_UNDEFINED when the thread failed to start
 */
error_t thread_start(Thread* thread);

/**
 * @brief Waits for the thread to finish execution.
 * @param[in] thread The thread instance.
 * @param[in] timeout The maximum time to wait in ticks.
 * @param[in] poll_interval The interval between status checks in ticks.
 * @retval ERROR_NONE when the thread was stopped
 * @retval ERROR_TIMEOUT when the thread was not stopped because the timeout has passed
 * @note Cannot be called from the thread being joined.
 */
error_t thread_join(Thread* thread, TickType_t timeout, TickType_t poll_interval);

/**
 * @brief Gets the FreeRTOS task handle associated with the thread.
 * @param[in] thread The thread instance.
 * @return The TaskHandle_t, or NULL if the thread is not running.
 */
TaskHandle_t thread_get_task_handle(Thread* thread);

/**
 * @brief Gets the return code from the thread's main function.
 * @param[in] thread The thread instance.
 * @return The return code of the thread's main function.
 * @note The thread must be in the STOPPED state.
 */
int32_t thread_get_return_code(Thread* thread);

/**
 * @brief Gets the minimum remaining stack space for the thread since it started.
 * @param[in] thread The thread instance.
 * @return The minimum remaining stack space in bytes.
 * @note The thread must be in the RUNNING state.
 */
uint32_t thread_get_stack_space(Thread* thread);

/**
 * @brief Gets the current thread instance.
 * @return A pointer to the current Thread instance, or NULL if not called from a thread created by this module.
 */
Thread* thread_get_current(void);

#ifdef __cplusplus
}
#endif
