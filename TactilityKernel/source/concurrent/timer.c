// SPDX-License-Identifier: Apache-2.0
#include <tactility/concurrent/timer.h>
#include <tactility/check.h>
#include <tactility/freertos/timers.h>
#include <stdlib.h>

struct Timer {
    TimerHandle_t handle;
    timer_callback_t callback;
    void* context;
};

static void timer_callback_internal(TimerHandle_t xTimer) {
    struct Timer* timer = (struct Timer*)pvTimerGetTimerID(xTimer);
    if (timer != NULL && timer->callback != NULL) {
        timer->callback(timer->context);
    }
}

struct Timer* timer_alloc(enum TimerType type, TickType_t ticks, timer_callback_t callback, void* context) {
    check(xPortInIsrContext() == pdFALSE);
    check(callback != NULL);

    struct Timer* timer = (struct Timer*)malloc(sizeof(struct Timer));
    if (timer == NULL) {
        return NULL;
    }

    timer->callback = callback;
    timer->context = context;

    BaseType_t auto_reload = (type == TIMER_TYPE_ONCE) ? pdFALSE : pdTRUE;
    timer->handle = xTimerCreate(NULL, ticks, auto_reload, timer, timer_callback_internal);

    if (timer->handle == NULL) {
        free(timer);
        return NULL;
    }

    return timer;
}

void timer_free(struct Timer* timer) {
    check(xPortInIsrContext() == pdFALSE);
    check(timer != NULL);
    // MAX_TICKS or a reasonable timeout for the timer command queue
    xTimerDelete(timer->handle, portMAX_DELAY);
    free(timer);
}

error_t timer_start(struct Timer* timer) {
    check(xPortInIsrContext() == pdFALSE);
    check(timer != NULL);
    return (xTimerStart(timer->handle, portMAX_DELAY) == pdPASS) ? ERROR_NONE : ERROR_TIMEOUT;
}

error_t timer_stop(struct Timer* timer) {
    check(xPortInIsrContext() == pdFALSE);
    check(timer != NULL);
    return (xTimerStop(timer->handle, portMAX_DELAY) == pdPASS) ? ERROR_NONE : ERROR_TIMEOUT;
}

error_t timer_reset_with_interval(struct Timer* timer, TickType_t interval) {
    check(xPortInIsrContext() == pdFALSE);
    check(timer != NULL);
    if (xTimerChangePeriod(timer->handle, interval, portMAX_DELAY) != pdPASS) {
        return ERROR_TIMEOUT;
    }
    return (xTimerReset(timer->handle, portMAX_DELAY) == pdPASS) ? ERROR_NONE : ERROR_TIMEOUT;
}

error_t timer_reset(struct Timer* timer) {
    check(xPortInIsrContext() == pdFALSE);
    check(timer != NULL);
    return (xTimerReset(timer->handle, portMAX_DELAY) == pdPASS) ? ERROR_NONE : ERROR_TIMEOUT;
}

bool timer_is_running(struct Timer* timer) {
    check(xPortInIsrContext() == pdFALSE);
    check(timer != NULL);
    return xTimerIsTimerActive(timer->handle) != pdFALSE;
}

TickType_t timer_get_expiry_time(struct Timer* timer) {
    check(xPortInIsrContext() == pdFALSE);
    check(timer != NULL);
    return xTimerGetExpiryTime(timer->handle);
}

error_t timer_set_pending_callback(struct Timer* timer, timer_pending_callback_t callback, void* context, uint32_t arg, TickType_t timeout) {
    (void)timer; // Unused in this implementation but kept for API consistency if needed later
    BaseType_t result;
    if (xPortInIsrContext() == pdTRUE) {
        check(timeout == 0);
        result = xTimerPendFunctionCallFromISR(callback, context, arg, NULL);
    } else {
        result = xTimerPendFunctionCall(callback, context, arg, timeout);
    }
    return (result == pdPASS) ? ERROR_NONE : ERROR_TIMEOUT;
}

void timer_set_callback_priority(struct Timer* timer, enum ThreadPriority priority) {
    (void)timer; // Unused in this implementation but kept for API consistency if needed later
    check(xPortInIsrContext() == pdFALSE);

    TaskHandle_t task_handle = xTimerGetTimerDaemonTaskHandle();
    check(task_handle != NULL);

    vTaskPrioritySet(task_handle, (UBaseType_t)priority);
}
