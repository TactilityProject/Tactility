// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

extern struct Module lvgl_module;

struct LvglModuleConfig {
    /** Used to add devices like displays/pointers/etc. */
    void (*on_start)(void);
    /** Used to remove devices */
    void (*on_stop)(void);
};

void lvgl_module_configure(const struct LvglModuleConfig config);

void lvgl_lock(void);

bool lvgl_try_lock_timed(uint32_t timeout);

void lvgl_unlock(void);

bool lvgl_is_running();

#ifdef __cplusplus
}
#endif
