#pragma once

#include <driver/gpio.h>
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
 * @brief Trackball configuration structure
 */
struct TrackballConfig {
    gpio_num_t pinRight;          // Right direction GPIO
    gpio_num_t pinUp;             // Up direction GPIO
    gpio_num_t pinLeft;           // Left direction GPIO
    gpio_num_t pinDown;           // Down direction GPIO
    gpio_num_t pinClick;          // Click/select button GPIO
    uint8_t encoderSensitivity = 1;   // Encoder mode: steps per tick
    uint8_t pointerSensitivity = 10;   // Pointer mode: pixels per tick
};

/**
 * @brief Initialize trackball as LVGL input device
 * @param config Trackball GPIO configuration
 * @return LVGL input device pointer, or nullptr on failure
 */
lv_indev_t* init(const TrackballConfig& config);

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
