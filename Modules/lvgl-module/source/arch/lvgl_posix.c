// SPDX-License-Identifier: Apache-2.0
#ifndef ESP_PLATFORM

#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/concurrent/thread.h>
#include <tactility/freertos/task.h>
#include <tactility/time.h>

#include <lvgl.h>

#include <lvgl/lvgl.h>
#include <lvgl/module.h>
#include <lvgl/devices/keyboard_private.h>
#include <png_decoder.h>

extern struct LvglModuleConfig lvgl_module_config;
extern void lvgl_devices_attach();
extern void lvgl_devices_detach();

// Mutex for LVGL drawing
static struct RecursiveMutex lvgl_mutex;
static bool lvgl_mutex_initialised = false;
static struct RecursiveMutex task_mutex;
static bool task_mutex_initialised = false;

static uint32_t task_max_sleep_ms = 10;
static TaskHandle_t lvgl_task_handle = NULL;
static bool lvgl_task_interrupt_requested = false;

// ESP32 gets LVGL's tick driven for free by esp_lvgl_port; POSIX has no such
// helper, so lv_tick_get() would otherwise stay at 0 forever and every timer
// (indev polling included) would look permanently "not due yet".
static uint32_t lvgl_last_tick_millis = 0;

#define LVGL_STOP_POLL_INTERVAL 10
#define LVGL_STOP_TIMEOUT 5000

static void task_lock(void) {
    if (!task_mutex_initialised) return;
    recursive_mutex_lock(&task_mutex);
}

static void task_unlock(void) {
    if (!task_mutex_initialised) return;
    recursive_mutex_unlock(&task_mutex);
}

void lvgl_lock(void) {
    if (!lvgl_mutex_initialised) return;
    recursive_mutex_lock(&lvgl_mutex);
}

bool lvgl_try_lock(uint32_t timeout) {
    if (!lvgl_mutex_initialised) return false;
    return recursive_mutex_try_lock(&lvgl_mutex, millis_to_ticks(timeout));
}

void lvgl_unlock(void) {
    if (!lvgl_mutex_initialised) return;
    recursive_mutex_unlock(&lvgl_mutex);
}

static void lvgl_task_set_interrupted(bool interrupted) {
    task_lock();
    lvgl_task_interrupt_requested = interrupted;
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

    // Must run from this task (like on_start below), otherwise the display doesn't work.
    lvgl_devices_attach();

    // on_start must be called from the task, otherwise the display doesn't work
    if (lvgl_module_config.on_start) lvgl_module_config.on_start();

    lvgl_last_tick_millis = (uint32_t)get_millis();

    while (!lvgl_task_is_interrupt_requested()) {
        uint32_t now_millis = (uint32_t)get_millis();
        lv_tick_inc(now_millis - lvgl_last_tick_millis);
        lvgl_last_tick_millis = now_millis;

        if (lvgl_try_lock(10)) {
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

    lvgl_devices_detach();

    task_lock();
    lvgl_task_handle = NULL;
    task_unlock();

    vTaskDelete(NULL);
}

error_t lvgl_arch_start() {
    if (!lvgl_mutex_initialised) {
        recursive_mutex_construct(&lvgl_mutex);
        lvgl_mutex_initialised = true;
    }

    if (!task_mutex_initialised) {
        recursive_mutex_construct(&task_mutex);
        task_mutex_initialised = true;
    }

    lvgl_task_set_interrupted(false);

    lv_init();
    lvgl_png_decoder_init();

    // Must exist before devices/services are attached from the lvgl task below,
    // since those can immediately try to assign an indev to this group.
    lvgl_keyboard_on_start_lvgl();

    // Create the main app loop, like ESP-IDF
    BaseType_t task_result = xTaskCreate(
        lvgl_task,
        "lvgl",
        lvgl_module_config.task_stack_size,
        NULL,
        lvgl_module_config.task_priority,
        &lvgl_task_handle
    );

    return (task_result == pdTRUE) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t lvgl_arch_stop() {
    TickType_t start_ticks = get_ticks();
    lvgl_task_set_interrupted(true);
    while (true) {
        task_lock();
        bool done = (lvgl_task_handle == NULL);
        task_unlock();
        if (done) break;

        delay_ticks(LVGL_STOP_POLL_INTERVAL);

        if (get_ticks() - start_ticks > LVGL_STOP_TIMEOUT) {
            return ERROR_TIMEOUT;
        }
    }

    lvgl_keyboard_on_stop_lvgl();

    lv_deinit();

    return ERROR_NONE;
}

#endif
