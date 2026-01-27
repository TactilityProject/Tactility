// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <tactility/freertos/semphr.h>
#include <tactility/check.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct RecursiveMutex {
    QueueHandle_t handle;
};

inline static void recursive_mutex_construct(struct RecursiveMutex* mutex) {
    mutex->handle = xSemaphoreCreateRecursiveMutex();
}

inline static void recursive_mutex_destruct(struct RecursiveMutex* mutex) {
    check(mutex->handle != NULL);
    check(xPortInIsrContext() != pdTRUE);
    vSemaphoreDelete(mutex->handle);
    mutex->handle = NULL;
}

inline static void recursive_mutex_lock(struct RecursiveMutex* mutex) {
    check(xPortInIsrContext() != pdTRUE);
    xSemaphoreTakeRecursive(mutex->handle, portMAX_DELAY);
}

inline static bool recursive_mutex_is_locked(struct RecursiveMutex* mutex) {
    if (xPortInIsrContext() == pdTRUE) {
        return xSemaphoreGetMutexHolderFromISR(mutex->handle) != NULL;
    } else {
        return xSemaphoreGetMutexHolder(mutex->handle) != NULL;
    }
}

inline static bool recursive_mutex_try_lock(struct RecursiveMutex* mutex) {
    check(xPortInIsrContext() != pdTRUE);
    return xSemaphoreTakeRecursive(mutex->handle, 0) == pdTRUE;
}

inline static bool recursive_mutex_try_lock_timed(struct RecursiveMutex* mutex, TickType_t timeout) {
    check(xPortInIsrContext() != pdTRUE);
    return xSemaphoreTakeRecursive(mutex->handle, timeout) == pdTRUE;
}

inline static void recursive_mutex_unlock(struct RecursiveMutex* mutex) {
    check(xPortInIsrContext() != pdTRUE);
    xSemaphoreGiveRecursive(mutex->handle);
}

#ifdef __cplusplus
}
#endif
