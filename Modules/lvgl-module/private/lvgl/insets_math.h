// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl/insets.h>

#include <tactility/drivers/display.h>

/** Calculates the insets of a display with the given shape and resolution */
LvglInsets lvgl_insets_calculate(const DisplayShape& shape, int32_t width, int32_t height);

/** Calculates the horizontal margin of a row of a display with the given shape and resolution */
int32_t lvgl_insets_calculate_row(const DisplayShape& shape, int32_t width, int32_t height, int32_t y);
