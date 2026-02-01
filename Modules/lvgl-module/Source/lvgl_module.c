// SPDX-License-Identifier: GPL-3.0-only
#include <string.h>
#include <tactility/module.h>
#include <tactility/lvgl_module.h>

error_t lvgl_arch_start();
error_t lvgl_arch_stop();

static bool is_running;

static struct LvglModuleConfig config = {
    nullptr,
    nullptr
};

void lvgl_module_configure(const struct LvglModuleConfig in_config) {
    memcpy(&config, &in_config, sizeof(struct LvglModuleConfig));
}

static error_t start() {
    if (is_running) {
        return ERROR_NONE;
    }

    error_t result = lvgl_arch_start();
    if (result == ERROR_NONE) {
        is_running = true;
        if (config.on_start) config.on_start();
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
        if (config.on_stop) config.on_stop();
    }

    return error;
}

struct Module lvgl_module = {
    .name = "lvgl",
    .start = start,
    .stop = stop
};
