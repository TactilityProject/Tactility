// SPDX-License-Identifier: GPL-3.0-only
#ifdef ESP_PLATFORM

void lvgl_lock(void) {
}

bool lvgl_try_lock_timed(uint32_t timeout) {
}

void lvgl_unlock(void) {
}

error_t lvgl_arch_start() {
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = static_cast<UBaseType_t>(Thread::Priority::Critical),
        .task_stack = LVGL_TASK_STACK_DEPTH,
        .task_affinity = getCpuAffinityConfiguration().graphics,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5
    };

    if (lvgl_port_init(&lvgl_cfg) != ESP_OK) {
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

error_t lvgl_arch_stop() {
    return ERROR_NONE;
}

#endif