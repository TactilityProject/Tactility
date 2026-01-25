// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <Tactility/FreeRTOS/semphr.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Mutex {
    QueueHandle_t handle;
    // TODO: Debugging functionality
};

inline static void mutex_construct(struct Mutex* mutex) {
    assert(mutex->handle == NULL);
    mutex->handle = xSemaphoreCreateMutex();
}

inline static void mutex_destruct(struct Mutex* mutex) {
    assert(mutex->handle != NULL);
    vPortAssertIfInISR();
    vSemaphoreDelete(mutex->handle);
    mutex->handle = NULL;
}

inline static void mutex_lock(struct Mutex* mutex) {
    assert(xPortInIsrContext() != pdTRUE);
    xSemaphoreTake(mutex->handle, portMAX_DELAY);
}

inline static bool mutex_try_lock(struct Mutex* mutex) {
    assert(xPortInIsrContext() != pdTRUE);
    return xSemaphoreTake(mutex->handle, 0) == pdTRUE;
}

inline static bool mutex_try_lock_timed(struct Mutex* mutex, TickType_t timeout) {
    assert(xPortInIsrContext() != pdTRUE);
    return xSemaphoreTake(mutex->handle, timeout) == pdTRUE;
}

inline static bool mutex_is_locked(struct Mutex* mutex) {
    if (xPortInIsrContext() == pdTRUE) {
        return xSemaphoreGetMutexHolder(mutex->handle) != NULL;
    } else {
        return xSemaphoreGetMutexHolderFromISR(mutex->handle) != NULL;
    }
}

inline static void mutex_unlock(struct Mutex* mutex) {
    assert(xPortInIsrContext() != pdTRUE);
    xSemaphoreGive(mutex->handle);
}

#ifdef __cplusplus
}
#endif
