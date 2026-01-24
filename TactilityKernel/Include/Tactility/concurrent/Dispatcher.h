// SPDX-License-Identifier: Apache-2.0

/**
* Dispatcher is a thread-safe code execution queue.
*/
#pragma once

#include <stdbool.h>
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
 * @param[in] callback the function to execute elsewhere
 * @param[in] callback_context the data to pass to the function upon execution
 * @param[in] timeout lock acquisition timeout
 * @return true if dispatching was successful (timeout not reached)
 */
bool dispatcher_dispatch_timed(DispatcherHandle_t dispatcher, void* callback_context, DispatcherCallback callback, TickType_t timeout);

/**
 * Queue a function to be consumed elsewhere.
 * @param[in] callback the function to execute elsewhere
 * @param[in] callback_context the data to pass to the function upon execution
 * @return true if dispatching was successful (timeout not reached)
 */
static inline bool dispatcher_dispatch(DispatcherHandle_t dispatcher, void* callback_context, DispatcherCallback callback) {
    return dispatcher_dispatch_timed(dispatcher, callback_context, callback, portMAX_DELAY);
}

/**
 * Consume 1 or more dispatched function (if any) until the queue is empty.
 * @warning The timeout is only the wait time before consuming the message! It is not a limit to the total execution time when calling this method.
 * @param[in] timeout the ticks to wait for a message
 * @return the amount of messages that were consumed
 */
uint32_t dispatcher_consume_timed(DispatcherHandle_t dispatcher, TickType_t timeout);

/**
 * Consume 1 or more dispatched function (if any) until the queue is empty.
 * @warning The timeout is only the wait time before consuming the message! It is not a limit to the total execution time when calling this method.
 * @return the amount of messages that were consumed
 */
static inline uint32_t dispatcher_consume(DispatcherHandle_t dispatcher) {
    return dispatcher_consume_timed(dispatcher, portMAX_DELAY);
}

#ifdef __cplusplus
}
#endif
