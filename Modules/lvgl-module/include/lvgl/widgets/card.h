// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a card: a container that groups related content and stands out from its background.
 * The theme styles it as a card. Plain containers (lv_obj_create()) don't stand out, as they share the background colour.
 * @param[in] parent the parent object for the new card
 * @return the created card
 */
lv_obj_t* lvgl_card_create(lv_obj_t* parent);

#ifdef __cplusplus
}
#endif
