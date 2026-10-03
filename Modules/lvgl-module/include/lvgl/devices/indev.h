// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Checks whether any registered LVGL input device is of the given type.
 * @warning Caller must hold the LVGL lock.
 * @param[in] type the input device type to look for
 * @return true if at least one input device of the given type exists
 */
bool lvgl_indev_exists(lv_indev_type_t type);

#ifdef __cplusplus
}
#endif
