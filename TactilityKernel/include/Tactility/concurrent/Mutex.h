#pragma once

#include <Tactility/FreeRTOS/semphr.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Mutex {
    QueueHandle_t handle;
};

inline static void mutex_construct(struct Mutex* mutex) {
    mutex->handle = xSemaphoreCreateMutex();
}

inline static void mutex_destruct(struct Mutex* mutex) {
    assert(mutex != NULL);
    assert(mutex->handle != NULL);
    vPortAssertIfInISR();
    vSemaphoreDelete(mutex->handle);
    mutex->handle = NULL;
}

inline static void mutex_lock(struct Mutex* mutex) {
    assert(mutex->handle != NULL);
    xSemaphoreTake(mutex->handle, portMAX_DELAY);
}

inline static bool mutex_try_lock(struct Mutex* mutex) {
    assert(mutex->handle != NULL);
    return xSemaphoreTake(mutex->handle, 0) == pdTRUE;
}

inline static bool mutex_is_locked(struct Mutex* mutex) {
    assert(mutex->handle != NULL);
    return xSemaphoreGetMutexHolder(mutex->handle) != NULL;
}

inline static void mutex_unlock(struct Mutex* mutex) {
    assert(mutex->handle != NULL);
    xSemaphoreGive(mutex->handle);
}

#ifdef __cplusplus
}
#endif
