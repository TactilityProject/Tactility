// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a focus container: a transparent container without padding that the theme only styles with a focus ring.
 * Use it to make a group of widgets selectable as one, e.g. with a click handler on the container.
 * @param[in] parent the parent object for the new container
 * @return the created container
 */
lv_obj_t* lvgl_focus_container_create(lv_obj_t* parent);

#ifdef __cplusplus
}
#endif
