#ifndef _EPAPER_H_
#define _EPAPER_H_

#include <stdint.h>
#include <stdbool.h>
#include "epaper_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle
typedef struct epd_device* epd_handle_t;

// Panel info (read-only after init)
typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t buffer_size;       // In bytes
    epd_color_mode_t color_mode;
} epd_panel_info_t;

/**
 * @brief Initialize e-paper display
 * @param config Device configuration
 * @param handle Output handle
 * @return ESP_OK on success
 */
esp_err_t epd_init(const epd_config_t *config, epd_handle_t *handle);

/**
 * @brief Deinitialize e-paper display
 */
esp_err_t epd_deinit(epd_handle_t handle);

/**
 * @brief Get panel information
 */
esp_err_t epd_get_info(epd_handle_t handle, epd_panel_info_t *info);

/**
 * @brief Clear display (full white)
 */
esp_err_t epd_clear(epd_handle_t handle);

/**
 * @brief Fill display with color (0x00=black, 0xFF=white)
 */
esp_err_t epd_fill(epd_handle_t handle, uint8_t color);

/**
 * @brief Update full screen with buffer
 * @param buffer Pixel data (1 bit per pixel, MSB first)
 * @param mode Update mode (full/fast/partial)
 */
esp_err_t epd_update(epd_handle_t handle, const uint8_t *buffer, epd_update_mode_t mode);

/**
 * @brief Partial update
 * @param x Start X (must be multiple of 8)
 * @param y Start Y
 * @param w Width (must be multiple of 8)
 * @param h Height
 * @param buffer Pixel data
 */
esp_err_t epd_update_partial(epd_handle_t handle, uint16_t x, uint16_t y, 
                              uint16_t w, uint16_t h, const uint8_t *buffer);

/**
 * @brief Set base image for partial updates
 */
esp_err_t epd_set_base_image(epd_handle_t handle, const uint8_t *buffer);

/**
 * @brief Enter deep sleep mode
 */
esp_err_t epd_sleep(epd_handle_t handle);

/**
 * @brief Wake from deep sleep
 */
esp_err_t epd_wake(epd_handle_t handle);

/**
 * @brief Check if display is busy
 */
bool epd_is_busy(epd_handle_t handle);

/**
 * @brief Wait until display is ready
 * @param timeout_ms Timeout in milliseconds (0 = infinite)
 */
esp_err_t epd_wait_busy(epd_handle_t handle, uint32_t timeout_ms);

// ============ LVGL Integration Interface ============

/**
 * @brief Flush callback for LVGL
 * Call this from lv_display_set_flush_cb
 * 
 * Usage in LVGL 9:
 *   lv_display_t *disp = lv_display_create(width, height);
 *   lv_display_set_flush_cb(disp, my_flush_cb);
 *   lv_display_set_user_data(disp, epd_handle);
 * 
 * In my_flush_cb:
 *   epd_handle_t h = lv_display_get_user_data(disp);
 *   epd_lvgl_flush(h, area, px_map);
 *   lv_display_flush_ready(disp);
 */
esp_err_t epd_lvgl_flush(epd_handle_t handle, const void *area, const uint8_t *px_map);

/**
 * @brief Get framebuffer for direct LVGL rendering
 * @return Pointer to internal framebuffer
 */
uint8_t* epd_get_framebuffer(epd_handle_t handle);

/**
 * @brief Flush framebuffer to display
 */
esp_err_t epd_flush_framebuffer(epd_handle_t handle, epd_update_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif // _EPAPER_H_
