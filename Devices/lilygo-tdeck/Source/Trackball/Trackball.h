#pragma once

#include <lvgl.h>

namespace trackball {

/**
 * @brief Trackball operating mode
 */
enum class Mode {
    Encoder,  // Navigation via enc_diff (scroll wheel behavior)
    Pointer   // Mouse cursor via point.x/y
};

/**
 * @brief Initialize trackball as an LVGL input device, backed by the kernel tdeck_trackball driver.
 * @return LVGL input device pointer, or nullptr if the kernel device isn't found/started
 */
lv_indev_t* init();

/**
 * @brief Deinitialize trackball
 */
void deinit();

/**
 * @brief Set encoder mode sensitivity
 * @param sensitivity Steps per trackball tick (1-10, default: 1)
 */
void setEncoderSensitivity(uint8_t sensitivity);

/**
 * @brief Set pointer mode sensitivity
 * @param sensitivity Pixels per trackball tick (1-10, default: 10)
 */
void setPointerSensitivity(uint8_t sensitivity);

/**
 * @brief Enable or disable trackball input processing
 * @param enabled Boolean value to enable or disable
 */
void setEnabled(bool enabled);

/**
 * @brief Set trackball operating mode
 * @param mode Encoder or Pointer mode
 */
void setMode(Mode mode);

/**
 * @brief Get current trackball operating mode
 * @return Current mode
 */
Mode getMode();

}
