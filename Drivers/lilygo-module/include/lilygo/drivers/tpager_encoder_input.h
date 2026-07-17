// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

namespace tpager_encoder {

/**
 * @brief Initialize the encoder wheel as an LVGL input device, backed by the kernel
 * tpager_encoder driver.
 * @return LVGL input device pointer, or nullptr if the kernel device isn't found/started
 */
lv_indev_t* init();

/**
 * @brief Deinitialize the encoder wheel's LVGL input device.
 */
void deinit();

}
