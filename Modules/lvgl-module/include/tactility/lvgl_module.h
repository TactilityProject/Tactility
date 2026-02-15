// SPDX-License-Identifier: Apache-2.0
#pragma once

/**
 * @file lvgl_module.h
 * @brief LVGL module for Tactility.
 *
 * This module manages the lifecycle of the LVGL library, including initialization,
 * task management, and thread-safety.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief The LVGL module instance.
 */
extern struct Module lvgl_module;

/**
 * @brief Configuration for the LVGL module.
 */
struct LvglModuleConfig {
    /**
     * @brief Callback invoked when the LVGL task starts.
     * Use this to add devices (e.g. displays, pointers), start services, create widgets, etc.
     */
    void (*on_start)(void);

    /**
     * @brief Callback invoked when the LVGL task stops.
     * Use this to remove devices, stop services, etc.
     */
    void (*on_stop)(void);

    /** @brief Priority of the LVGL task. */
    int task_priority;

    /** @brief Stack size of the LVGL task in bytes. */
    int task_stack_size;

#ifdef ESP_PLATFORM
    /** @brief CPU affinity of the LVGL task (ESP32 specific). */
    int task_affinity;
#endif
};

/**
 * @brief Configures the LVGL module.
 *
 * @warning This must be called before starting the module.
 * @param config The configuration to apply.
 */
void lvgl_module_configure(struct LvglModuleConfig config);

/**
 * @brief Locks the LVGL mutex.
 *
 * This should be called before any LVGL API calls from threads other than the LVGL task.
 * It is a recursive mutex.
 * @retval true when a lock was acquired, false otherwise
 */
bool lvgl_lock(void);

/**
 * @brief Tries to lock the LVGL mutex with a timeout.
 *
 * @param timeout Timeout in ticks
 * @return true if the lock was acquired, false otherwise.
 */
bool lvgl_try_lock(uint32_t timeout);

/**
 * @brief Unlocks the LVGL mutex.
 */
void lvgl_unlock(void);

/**
 * @brief Checks if the LVGL module is currently running.
 *
 * @return true if running, false otherwise.
 */
bool lvgl_is_running(void);

#ifdef __cplusplus
}
#endif
