#include "LvglTask.h"

#include <Tactility/Logger.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/Thread.h>
#include <tactility/check.h>
#include <Tactility/lvgl/LvglSync.h>

#include <lvgl.h>

static const auto LOGGER = tt::Logger("LvglTask");

// Mutex for LVGL drawing
static tt::RecursiveMutex lvgl_mutex;
static tt::RecursiveMutex task_mutex;

static uint32_t task_max_sleep_ms = 10;
// Mutex for LVGL task state (to modify task_running state)
static bool task_running = false;

lv_disp_t* displayHandle = nullptr;

static void lvgl_task(void* arg);

static bool task_lock(TickType_t timeout) {
    return task_mutex.lock(timeout);
}

static void task_unlock() {
    task_mutex.unlock();
}

static void task_set_running(bool running) {
    assert(task_lock(configTICK_RATE_HZ / 100));
    task_running = running;
    task_unlock();
}

bool lvgl_task_is_running() {
    assert(task_lock(configTICK_RATE_HZ / 100));
    bool result = task_running;
    task_unlock();
    return result;
}

static bool lvgl_lock(uint32_t timeoutMillis) {
    return lvgl_mutex.lock(pdMS_TO_TICKS(timeoutMillis));
}

static void lvgl_unlock() {
    lvgl_mutex.unlock();
}

void lvgl_task_interrupt() {
    check(task_lock(portMAX_DELAY));
    task_set_running(false); // interrupt task with boolean as flag
    task_unlock();
}

void lvgl_task_start() {
    LOGGER.info("LVGL task starting");

    tt::lvgl::syncSet(&lvgl_lock, &lvgl_unlock);

    // Create the main app loop, like ESP-IDF
    BaseType_t task_result = xTaskCreate(
        lvgl_task,
        "lvgl",
        8192,
        nullptr,
        static_cast<UBaseType_t>(tt::Thread::Priority::High), // Should be higher than main app task
        nullptr
    );

    assert(task_result == pdTRUE);
}

static void lvgl_task(void* arg) {
    LOGGER.info("LVGL task started");

    /** Ideally. the display handle would be created during Simulator.start(),
     * but somehow that doesn't work. Waiting here from a ThreadFlag when that happens
     * also doesn't work. It seems that it must be called from this task. */
    displayHandle = lv_sdl_window_create(320, 240);
    lv_sdl_window_set_title(displayHandle, "Tactility");

    uint32_t task_delay_ms = task_max_sleep_ms;

    task_set_running(true);

    while (lvgl_task_is_running()) {
        if (lvgl_lock(10)) {
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

    lv_disp_remove(displayHandle);
    displayHandle = nullptr;
    vTaskDelete(nullptr);
}

