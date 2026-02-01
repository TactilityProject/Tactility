// SPDX-License-Identifier: GPL-3.0-only
#ifndef ESP_PLATFORM

#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/concurrent/thread.h>

#include <tactility/freertos/task.h>

#include <lvgl.h>

#include "tactility/lvgl_module.h"

extern struct LvglModuleConfig lvgl_module_config;

// Mutex for LVGL drawing
static struct RecursiveMutex lvgl_mutex;
static bool lvgl_mutex_initialised = false;
static struct RecursiveMutex task_mutex;
static bool task_mutex_initialised = false;

static uint32_t task_max_sleep_ms = 10;
static TaskHandle_t lvgl_task_handle = NULL;
static bool lvgl_task_interrupt_requested = false;

#define LVGL_STOP_POLL_INTERVAL 10
#define LVGL_STOP_TIMEOUT 5000

static void task_lock(void) {
    if (!task_mutex_initialised) {
        recursive_mutex_construct(&task_mutex);
        task_mutex_initialised = true;
    }
    recursive_mutex_lock(&task_mutex);
}

static void task_unlock(void) {
    recursive_mutex_unlock(&task_mutex);
}

bool lvgl_lock(void) {
    if (!lvgl_mutex_initialised) {
        recursive_mutex_construct(&lvgl_mutex);
        lvgl_mutex_initialised = true;
    }
    recursive_mutex_lock(&lvgl_mutex);
    return true;
}

bool lvgl_try_lock_timed(uint32_t timeout) {
    if (!lvgl_mutex_initialised) {
        recursive_mutex_construct(&lvgl_mutex);
        lvgl_mutex_initialised = true;
    }

    return recursive_mutex_try_lock_timed(&lvgl_mutex, timeout);
}

void lvgl_unlock(void) {
    recursive_mutex_unlock(&lvgl_mutex);
}

static void lvgl_task_interrupt() {
    task_lock();
    lvgl_task_interrupt_requested = false; // interrupt task with boolean as flag
    task_unlock();
}

static bool lvgl_task_is_interrupt_requested() {
    task_lock();
    bool result = lvgl_task_interrupt_requested;
    task_unlock();
    return result;
}

static void lvgl_task(void* arg) {
    uint32_t task_delay_ms = task_max_sleep_ms;

    check(!lvgl_task_is_interrupt_requested());

    // on_start must be called from the task, otherwhise the display doesn't work
    if (lvgl_module_config.on_start) lvgl_module_config.on_start();

    while (!lvgl_task_is_interrupt_requested()) {
        if (lvgl_try_lock_timed(10)) {
            task_delay_ms = lv_timer_handler();
            lvgl_unlock();
        }
        if ((task_delay_ms > task_max_sleep_ms) || (1 == task_delay_ms)) {
            task_delay_ms = task_max_sleep_ms;
        } else if (task_delay_ms < 1) {
            task_delay_ms = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }

    if (lvgl_module_config.on_stop) lvgl_module_config.on_stop();

    vTaskDelete(nullptr);
}

error_t lvgl_arch_start() {
    lv_init();

    // Create the main app loop, like ESP-IDF
    BaseType_t task_result = xTaskCreate(
        lvgl_task,
        "lvgl",
        lvgl_module_config.task_stack_size,
        &lvgl_task_handle,
        lvgl_module_config.task_priority,
        nullptr
    );

    return (task_result == pdTRUE) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t lvgl_arch_stop() {
    TickType_t start_ticks = get_ticks();
    lvgl_task_interrupt();
    while (lvgl_task_handle != nullptr) { // TODO: make thread-safe
        delay_ticks(LVGL_STOP_POLL_INTERVAL);
        if (get_ticks() - start_ticks > LVGL_STOP_TIMEOUT) {
            return ERROR_TIMEOUT;
        }
    }

    lv_deinit();

    return ERROR_NONE;
}

#endif
