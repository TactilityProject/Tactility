#pragma once

#include <stdbool.h>
#include <assert.h>
#include <Tactility/FreeRTOS/semphr.h>

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
    assert(mutex != NULL);
    assert(mutex->handle != NULL);
    vPortAssertIfInISR();
    vSemaphoreDelete(mutex->handle);
    mutex->handle = NULL;
}

inline static void recursive_mutex_lock(struct RecursiveMutex* mutex) {
    assert(mutex->handle != NULL);
    xSemaphoreTakeRecursive(mutex->handle, portMAX_DELAY);
}

inline static bool recursive_mutex_is_locked(struct RecursiveMutex* mutex) {
    assert(mutex->handle != NULL);
    return xSemaphoreGetMutexHolder(mutex->handle) != NULL;
}

inline static bool recursive_mutex_try_lock(struct RecursiveMutex* mutex) {
    assert(mutex->handle != NULL);
    return xSemaphoreTakeRecursive(mutex->handle, 0) == pdTRUE;
}

inline static void recursive_mutex_unlock(struct RecursiveMutex* mutex) {
    assert(mutex->handle != NULL);
    xSemaphoreGiveRecursive(mutex->handle);
}

#ifdef __cplusplus
}
#endif
