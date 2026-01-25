// SPDX-License-Identifier: Apache-2.0

/**
* Dispatcher is a thread-safe code execution queue.
*/
#pragma once

#include <stdbool.h>

#include <Tactility/Error.h>
#include <Tactility/FreeRTOS/FreeRTOS.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*DispatcherCallback)(void* context);
typedef void* DispatcherHandle_t;

DispatcherHandle_t dispatcher_alloc();

void dispatcher_free(DispatcherHandle_t dispatcher);

/**
 * Queue a function to be consumed elsewhere.
 * @param[in] callbackContext the data to pass to the function upon execution
 * @param[in] callback the function to execute elsewhere
 * @param[in] timeout lock acquisition timeout
 * @retval ERROR_TIMEOUT
 * @retval ERROR_INVALID_STATE when the dispatcher is in the process of shutting down
 * @retval ERROR_NONE
 */
error_t dispatcher_dispatch_timed(DispatcherHandle_t dispatcher, void* callbackContext, DispatcherCallback callback, TickType_t timeout);

/**
 * Queue a function to be consumed elsewhere.
 * @param[in] callbackContext the data to pass to the function upon execution
 * @param[in] callback the function to execute elsewhere
 * @retval ERROR_TIMEOUT unlikely to occur unless there's an issue with the internal mutex
 * @retval ERROR_INVALID_STATE when the dispatcher is in the process of shutting down
 * @retval ERROR_NONE
 */
static inline error_t dispatcher_dispatch(DispatcherHandle_t dispatcher, void* callbackContext, DispatcherCallback callback) {
    return dispatcher_dispatch_timed(dispatcher, callbackContext, callback, portMAX_DELAY);
}

/**
 * Consume 1 or more dispatched function (if any) until the queue is empty.
 * @warning The timeout is only the wait time before consuming the message! It is not a limit to the total execution time when calling this method.
 * @param[in] timeout the ticks to wait for a message
 * @retval ERROR_TIMEOUT
 * @retval ERROR_RESOURCE failed to wait for event
 * @retval ERROR_INVALID_STATE when the dispatcher is in the process of shutting down
 * @retval ERROR_NONE
 */
error_t dispatcher_consume_timed(DispatcherHandle_t dispatcher, TickType_t timeout);

/**
 * Consume 1 or more dispatched function (if any) until the queue is empty.
 * @warning The timeout is only the wait time before consuming the message! It is not a limit to the total execution time when calling this method.
 * @retval ERROR_TIMEOUT unlikely to occur unless there's an issue with the internal mutex
 * @retval ERROR_RESOURCE failed to wait for event
 * @retval ERROR_INVALID_STATE when the dispatcher is in the process of shutting down
 * @retval ERROR_NONE
 */
static inline error_t dispatcher_consume(DispatcherHandle_t dispatcher) {
    return dispatcher_consume_timed(dispatcher, portMAX_DELAY);
}

#ifdef __cplusplus
}
#endif
