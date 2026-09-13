#ifndef _EPAPER_LVGL_H_
#define _EPAPER_LVGL_H_

#include "epaper.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LVGL e-paper port configuration
 */
typedef struct {
    epd_handle_t epd;               // E-paper handle (required)
    epd_update_mode_t update_mode;  // Default update mode
    epd_dither_mode_t dither_mode;  // Dithering algorithm
    bool use_partial_refresh;       // Enable partial refresh optimization
    uint32_t partial_threshold;     // Min dirty area (pixels) for partial refresh
} epd_lvgl_config_t;

#define EPD_LVGL_CONFIG_DEFAULT() { \
    .epd = NULL, \
    .update_mode = EPD_UPDATE_FULL, \
    .dither_mode = EPD_DITHER_NONE, \
    .use_partial_refresh = false, \
    .partial_threshold = 1000, \
}

/**
 * @brief Initialize LVGL display driver for e-paper
 * 
 * @param config LVGL port configuration
 * @return lv_display_t* LVGL display object, NULL on failure
 * 
 * Example:
 * @code
 * epd_config_t epd_cfg = EPD_CONFIG_DEFAULT();
 * epd_handle_t epd;
 * epd_init(&epd_cfg, &epd);
 * 
 * epd_lvgl_config_t lvgl_cfg = EPD_LVGL_CONFIG_DEFAULT();
 * lvgl_cfg.epd = epd;
 * lvgl_cfg.use_partial_refresh = true;
 * 
 * lv_display_t *disp = epd_lvgl_init(&lvgl_cfg);
 * @endcode
 */
lv_display_t* epd_lvgl_init(const epd_lvgl_config_t *config);

/**
 * @brief Deinitialize LVGL e-paper driver
 */
void epd_lvgl_deinit(lv_display_t *disp);

/**
 * @brief Set update mode for next refresh
 * @param disp LVGL display
 * @param mode EPD_UPDATE_FULL, EPD_UPDATE_FAST, EPD_UPDATE_PARTIAL
 */
void epd_lvgl_set_update_mode(lv_display_t *disp, epd_update_mode_t mode);

/**
 * @brief Force full refresh on next update
 * Call this periodically (e.g., every 10 partial updates) to prevent ghosting
 */
void epd_lvgl_force_full_refresh(lv_display_t *disp);

/**
 * @brief Manually trigger display refresh
 * Use when lv_refr_now() doesn't work as expected
 */
void epd_lvgl_refresh(lv_display_t *disp);

/**
 * @brief Get e-paper handle from display
 */
epd_handle_t epd_lvgl_get_epd(lv_display_t *disp);

/**
 * @brief Set dithering mode
 * @param disp LVGL display
 * @param mode EPD_DITHER_NONE, EPD_DITHER_FLOYD_STEINBERG
 * 
 * Floyd-Steinberg dithering provides better image quality for photos/gradients
 * but requires additional memory (width * height * 3 bytes for RGB buffer)
 */
void epd_lvgl_set_dither_mode(lv_display_t *disp, epd_dither_mode_t mode);

/**
 * @brief Convert RGB888 to nearest e-paper color
 * @param r Red component (0-255)
 * @param g Green component (0-255)  
 * @param b Blue component (0-255)
 * @param mode Color mode of the panel
 * @return Nearest EPD_PIXEL_xxx value
 */
uint8_t epd_rgb_to_epaper_color(uint8_t r, uint8_t g, uint8_t b, epd_color_mode_t mode);

/**
 * @brief Create LVGL color from e-paper palette
 * @param epaper_color EPD_PIXEL_xxx value
 * @return lv_color_t LVGL color value
 */
lv_color_t epd_epaper_to_lv_color(uint8_t epaper_color);

#ifdef __cplusplus
}
#endif

#endif // _EPAPER_LVGL_H_
