// SPDX-License-Identifier: GPL-3.0-only
#include <string.h>
#include <tactility/module.h>
#include <tactility/lvgl_module.h>

error_t lvgl_arch_start();
error_t lvgl_arch_stop();

static bool is_running;

struct LvglModuleConfig lvgl_module_config = {
    nullptr,
    nullptr
};

void lvgl_module_configure(const struct LvglModuleConfig config) {
    memcpy(&lvgl_module_config, &config, sizeof(struct LvglModuleConfig));
}

static error_t start() {
    if (is_running) {
        return ERROR_NONE;
    }

    error_t result = lvgl_arch_start();
    if (result == ERROR_NONE) {
        is_running = true;
    }

    return result;
}

static error_t stop() {
    if (!is_running) {
        return ERROR_NONE;
    }

    error_t error = lvgl_arch_stop();
    if (error == ERROR_NONE) {
        is_running = false;
    }

    return error;
}

struct Module lvgl_module = {
    .name = "lvgl",
    .start = start,
    .stop = stop
};
