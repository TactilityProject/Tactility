// SPDX-License-Identifier: Apache-2.0
#ifdef ESP_PLATFORM

#include <esp_lvgl_port.h>

#include <lvgl/lvgl.h>
#include <lvgl/module.h>
#include <lvgl/devices/keyboard_private.h>
#include <png_decoder.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/time.h>

#define TAG "lvgl_esp32"

extern struct LvglModuleConfig lvgl_module_config;
extern void lvgl_devices_attach();
extern void lvgl_devices_detach();

static bool initialized = false;

void lvgl_lock(void) {
    if (!initialized) { return; }
    lvgl_port_lock(portMAX_DELAY);
}

bool lvgl_try_lock(uint32_t timeoutTicks) {
    if (!initialized) { return false; }
    // lvgl_port_lock expects milliseconds
    return lvgl_port_lock(timeoutTicks * portTICK_PERIOD_MS);
}

void lvgl_unlock(void) {
    if (!initialized) { return; }
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

    lvgl_lock();
    lvgl_png_decoder_init();
    lvgl_unlock();

    // Must exist before devices/services are attached below, since those can
    // immediately try to assign an indev to this group (e.g. USB HID input).
    lvgl_keyboard_on_start_lvgl();

    lvgl_devices_attach();

    if (lvgl_module_config.on_start) lvgl_module_config.on_start();

    return ERROR_NONE;
}

error_t lvgl_arch_stop() {
    if (lvgl_module_config.on_stop) lvgl_module_config.on_stop();

    lvgl_devices_detach();

    lvgl_keyboard_on_stop_lvgl();

    if (lvgl_port_deinit() != ESP_OK) {
        // Recreate what stop() above tore down, then call on_start again to recover
        lvgl_keyboard_on_start_lvgl();
        if (lvgl_module_config.on_start) lvgl_module_config.on_start();
        return ERROR_RESOURCE;
    }

    initialized = false;
    return ERROR_NONE;
}

#endif