#pragma once

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
    vSemaphoreDelete(mutex->handle);
    mutex->handle = NULL;
}

inline static void recursive_mutex_lock(struct RecursiveMutex* mutex) {
    assert(mutex->handle != NULL);
    xSemaphoreTake(mutex->handle, portMAX_DELAY);
}

inline static bool recursive_mutex_is_locked(struct RecursiveMutex* mutex) {
    assert(mutex->handle != NULL);
    return xSemaphoreGetMutexHolder(mutex->handle) != NULL;
}

inline static bool recursive_mutex_try_lock(struct RecursiveMutex* mutex) {
    assert(mutex->handle != NULL);
    return xSemaphoreTake(mutex->handle, 0) == pdTRUE;
}

inline static void recursive_mutex_unlock(struct RecursiveMutex* mutex) {
    assert(mutex->handle != NULL);
    xSemaphoreGive(mutex->handle);
}

#ifdef __cplusplus
}
#endif
