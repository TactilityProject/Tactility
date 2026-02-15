// SPDX-License-Identifier: GPL-3.0-only
#ifdef ESP_PLATFORM

#include <esp_lvgl_port.h>

#include <tactility/time.h>
#include <tactility/error.h>
#include <tactility/lvgl_module.h>

extern struct LvglModuleConfig lvgl_module_config;

static bool initialized = false;

bool lvgl_lock(void) {
    if (!initialized) return true; // We allow (fake) locking because it's safe to do so as LVGL is not running yet
    return lvgl_port_lock(portMAX_DELAY);
}

bool lvgl_try_lock(uint32_t timeout) {
    if (!initialized) return true; // We allow (fake) locking because it's safe to do so as LVGL is not running yet
    return lvgl_port_lock(millis_to_ticks(timeout));
}

void lvgl_unlock(void) {
    if (!initialized) return;
    lvgl_port_unlock();
}

error_t lvgl_arch_start() {
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = lvgl_module_config.task_priority,
        .task_stack = lvgl_module_config.task_stack_size,
        .task_affinity = lvgl_module_config.task_affinity,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5
    };

    if (lvgl_port_init(&lvgl_cfg) != ESP_OK) {
        return ERROR_RESOURCE;
    }

    // We must have the state set to "initialized" so that the lvgl lock works
    // when we call listener functions. These functions could create new
    // devices and services. The latter might start adding widgets immediately.
    initialized = true;

    if (lvgl_module_config.on_start) lvgl_module_config.on_start();

    return ERROR_NONE;
}

error_t lvgl_arch_stop() {
    if (lvgl_module_config.on_stop) lvgl_module_config.on_stop();

    if (lvgl_port_deinit() != ESP_OK) {
        // Call on_start again to recover
        if (lvgl_module_config.on_start) lvgl_module_config.on_start();
        return ERROR_RESOURCE;
    }

    initialized = false;

    return ERROR_NONE;
}

#endif