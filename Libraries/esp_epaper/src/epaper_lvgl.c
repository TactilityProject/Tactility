#include "epaper_lvgl.h"
#include "epaper_panel.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "epd_lvgl";

// External function to get panel descriptor
extern const epd_panel_desc_t* epd_get_panel(void *dev);

/*=============================================================================
 * Color Palette for Multi-Color E-Paper
 *============================================================================*/

// Pre-defined RGB values for e-paper colors
static const struct {
    uint8_t r, g, b;
    uint8_t epaper_color;
} color_palette[] = {
    {0,   0,   0,   EPD_PIXEL_BLACK},   // Black
    {255, 255, 255, EPD_PIXEL_WHITE},   // White
    {255, 255, 0,   EPD_PIXEL_YELLOW},  // Yellow
    {255, 0,   0,   EPD_PIXEL_RED},     // Red
    {0,   0,   255, EPD_PIXEL_BLUE},    // Blue
    {0,   255, 0,   EPD_PIXEL_GREEN},   // Green
};

#define PALETTE_SIZE (sizeof(color_palette) / sizeof(color_palette[0]))

// BW-only palette
static const struct {
    uint8_t r, g, b;
    uint8_t epaper_color;
} bw_palette[] = {
    {0,   0,   0,   EPD_PIXEL_BLACK},
    {255, 255, 255, EPD_PIXEL_WHITE},
};

#define BW_PALETTE_SIZE 2

/*=============================================================================
 * Port Context
 *============================================================================*/

typedef struct {
    epd_handle_t epd;
    epd_update_mode_t update_mode;
    epd_dither_mode_t dither_mode;
    epd_color_mode_t color_mode;
    uint8_t bits_per_pixel;     // From panel descriptor
    bool use_partial_refresh;
    uint32_t partial_threshold;
    uint8_t *lvgl_buf;      // RGB565 buffer for LVGL
    uint8_t *rgb_buf;       // RGB888 buffer for dithering
    uint32_t lvgl_buf_size;
    uint16_t width;
    uint16_t height;
    bool force_full;
    uint32_t partial_count;
    bool partial_render_mode;  // True if using small buffer with partial rendering
    bool fb_dirty;             // Framebuffer has been modified
} epd_lvgl_ctx_t;

/*=============================================================================
 * Color Conversion Functions
 *============================================================================*/

static inline int clamp_byte(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

// 4-color BWRY palette
static const struct {
    uint8_t r, g, b;
    uint8_t epaper_color;
} bwry_palette[] = {
    {0,   0,   0,   EPD_PIXEL_BLACK},   // Black
    {255, 255, 255, EPD_PIXEL_WHITE},   // White
    {255, 255, 0,   EPD_PIXEL_YELLOW},  // Yellow
    {255, 0,   0,   EPD_PIXEL_RED},     // Red
};

#define BWRY_PALETTE_SIZE 4

// Find nearest palette color (returns palette index)
static uint8_t find_nearest_color_idx(int r, int g, int b, epd_color_mode_t mode)
{
    uint32_t min_dist = UINT32_MAX;
    uint8_t best = 0;
    
    if (mode == EPD_COLOR_BW) {
        // BW mode: simple threshold
        int gray = (r * 299 + g * 587 + b * 114) / 1000;
        return (gray < 128) ? 0 : 1;  // 0=black, 1=white
    }
    
    if (mode == EPD_COLOR_4COLOR) {
        // 4-color BWRY mode
        for (int i = 0; i < BWRY_PALETTE_SIZE; i++) {
            int dr = r - bwry_palette[i].r;
            int dg = g - bwry_palette[i].g;
            int db = b - bwry_palette[i].b;
            uint32_t dist = dr * dr + dg * dg + db * db;
            
            if (dist < min_dist) {
                min_dist = dist;
                best = i;
            }
        }
        return best;
    }
    
    // Multi-color mode (6-color, 7-color)
    for (int i = 0; i < PALETTE_SIZE; i++) {
        int dr = r - color_palette[i].r;
        int dg = g - color_palette[i].g;
        int db = b - color_palette[i].b;
        uint32_t dist = dr * dr + dg * dg + db * db;
        
        if (dist < min_dist) {
            min_dist = dist;
            best = i;
        }
    }
    return best;
}

// Convert RGB to e-paper color
uint8_t epd_rgb_to_epaper_color(uint8_t r, uint8_t g, uint8_t b, epd_color_mode_t mode)
{
    if (mode == EPD_COLOR_BW) {
        int gray = (r * 299 + g * 587 + b * 114) / 1000;
        return (gray < 128) ? EPD_PIXEL_BLACK : EPD_PIXEL_WHITE;
    }
    
    if (mode == EPD_COLOR_4COLOR) {
        uint8_t idx = find_nearest_color_idx(r, g, b, mode);
        return bwry_palette[idx].epaper_color;
    }
    
    uint8_t idx = find_nearest_color_idx(r, g, b, mode);
    return color_palette[idx].epaper_color;
}

// Convert e-paper color to LVGL color
lv_color_t epd_epaper_to_lv_color(uint8_t epaper_color)
{
    switch (epaper_color) {
        case EPD_PIXEL_BLACK:  return lv_color_make(0, 0, 0);
        case EPD_PIXEL_WHITE:  return lv_color_make(255, 255, 255);
        case EPD_PIXEL_YELLOW: return lv_color_make(255, 255, 0);
        case EPD_PIXEL_RED:    return lv_color_make(255, 0, 0);
        case EPD_PIXEL_BLUE:   return lv_color_make(0, 0, 255);
        case EPD_PIXEL_GREEN:  return lv_color_make(0, 255, 0);
        default:               return lv_color_make(255, 255, 255);
    }
}

/*=============================================================================
 * Dithering Algorithms
 *============================================================================*/

// Bayer 4x4 ordered dithering matrix (values 0-15, scaled to 0-255)
static const uint8_t BAYER_4X4[4][4] = {
    {   0, 128,  32, 160 },
    { 192,  64, 224,  96 },
    {  48, 176,  16, 144 },
    { 240, 112, 208,  80 }
};

// Set pixel in framebuffer based on color mode
static inline void set_fb_pixel(uint8_t *fb, int x, int y, int width, uint8_t color, epd_color_mode_t mode)
{
    if (mode == EPD_COLOR_6COLOR || mode == EPD_COLOR_7COLOR) {
        // 4-bit per pixel (2 pixels per byte)
        uint32_t addr = (y * width + x) / 2;
        if (x % 2 == 0) {
            // High nibble (left pixel)
            fb[addr] = (fb[addr] & 0x0F) | (color << 4);
        } else {
            // Low nibble (right pixel)
            fb[addr] = (fb[addr] & 0xF0) | (color & 0x0F);
        }
    } else if (mode == EPD_COLOR_4COLOR || mode == EPD_COLOR_4GRAY) {
        // 2-bit per pixel (4 pixels per byte)
        // Pixel order: [P0:7-6][P1:5-4][P2:3-2][P3:1-0]
        uint32_t pixel_idx = y * width + x;
        uint32_t byte_idx = pixel_idx / 4;
        uint8_t bit_offset = (3 - (pixel_idx % 4)) * 2;  // 6, 4, 2, 0
        uint8_t mask = 0x03 << bit_offset;
        fb[byte_idx] = (fb[byte_idx] & ~mask) | ((color & 0x03) << bit_offset);
    } else {
        // 1-bit per pixel (BW)
        uint16_t byte_idx = y * (width / 8) + (x / 8);
        uint8_t bit = 7 - (x % 8);
        if (color == EPD_PIXEL_WHITE) {
            fb[byte_idx] |= (1 << bit);
        } else {
            fb[byte_idx] &= ~(1 << bit);
        }
    }
}

static void apply_floyd_steinberg_dithering(epd_lvgl_ctx_t *ctx, uint8_t *fb)
{
    if (!ctx->rgb_buf || !fb) return;
    
    ESP_LOGI(TAG, "Applying Floyd-Steinberg dithering...");
    
    for (int y = 0; y < ctx->height; y++) {
        // Yield every 20 rows to prevent watchdog
        if (y % 20 == 0) {
            vTaskDelay(1);
        }
        
        for (int x = 0; x < ctx->width; x++) {
            int idx = (y * ctx->width + x) * 3;
            
            // Get current pixel RGB
            int r = ctx->rgb_buf[idx + 0];
            int g = ctx->rgb_buf[idx + 1];
            int b = ctx->rgb_buf[idx + 2];
            
            // Find nearest palette color
            uint8_t pal_idx = find_nearest_color_idx(r, g, b, ctx->color_mode);
            uint8_t epaper_color;
            int pal_r, pal_g, pal_b;
            
            if (ctx->color_mode == EPD_COLOR_BW) {
                epaper_color = (pal_idx == 0) ? EPD_PIXEL_BLACK : EPD_PIXEL_WHITE;
                pal_r = bw_palette[pal_idx].r;
                pal_g = bw_palette[pal_idx].g;
                pal_b = bw_palette[pal_idx].b;
            } else if (ctx->color_mode == EPD_COLOR_4COLOR) {
                epaper_color = bwry_palette[pal_idx].epaper_color;
                pal_r = bwry_palette[pal_idx].r;
                pal_g = bwry_palette[pal_idx].g;
                pal_b = bwry_palette[pal_idx].b;
            } else {
                epaper_color = color_palette[pal_idx].epaper_color;
                pal_r = color_palette[pal_idx].r;
                pal_g = color_palette[pal_idx].g;
                pal_b = color_palette[pal_idx].b;
            }
            
            // Set pixel in framebuffer
            set_fb_pixel(fb, x, y, ctx->width, epaper_color, ctx->color_mode);
            
            // Calculate quantization error
            int err_r = r - pal_r;
            int err_g = g - pal_g;
            int err_b = b - pal_b;
            
            // Distribute error to neighbors (Floyd-Steinberg)
            // Right pixel: 7/16
            if (x + 1 < ctx->width) {
                int ni = idx + 3;
                ctx->rgb_buf[ni + 0] = clamp_byte(ctx->rgb_buf[ni + 0] + err_r * 7 / 16);
                ctx->rgb_buf[ni + 1] = clamp_byte(ctx->rgb_buf[ni + 1] + err_g * 7 / 16);
                ctx->rgb_buf[ni + 2] = clamp_byte(ctx->rgb_buf[ni + 2] + err_b * 7 / 16);
            }
            // Bottom-left pixel: 3/16
            if (y + 1 < ctx->height && x > 0) {
                int ni = ((y + 1) * ctx->width + x - 1) * 3;
                ctx->rgb_buf[ni + 0] = clamp_byte(ctx->rgb_buf[ni + 0] + err_r * 3 / 16);
                ctx->rgb_buf[ni + 1] = clamp_byte(ctx->rgb_buf[ni + 1] + err_g * 3 / 16);
                ctx->rgb_buf[ni + 2] = clamp_byte(ctx->rgb_buf[ni + 2] + err_b * 3 / 16);
            }
            // Bottom pixel: 5/16
            if (y + 1 < ctx->height) {
                int ni = ((y + 1) * ctx->width + x) * 3;
                ctx->rgb_buf[ni + 0] = clamp_byte(ctx->rgb_buf[ni + 0] + err_r * 5 / 16);
                ctx->rgb_buf[ni + 1] = clamp_byte(ctx->rgb_buf[ni + 1] + err_g * 5 / 16);
                ctx->rgb_buf[ni + 2] = clamp_byte(ctx->rgb_buf[ni + 2] + err_b * 5 / 16);
            }
            // Bottom-right pixel: 1/16
            if (y + 1 < ctx->height && x + 1 < ctx->width) {
                int ni = ((y + 1) * ctx->width + x + 1) * 3;
                ctx->rgb_buf[ni + 0] = clamp_byte(ctx->rgb_buf[ni + 0] + err_r / 16);
                ctx->rgb_buf[ni + 1] = clamp_byte(ctx->rgb_buf[ni + 1] + err_g / 16);
                ctx->rgb_buf[ni + 2] = clamp_byte(ctx->rgb_buf[ni + 2] + err_b / 16);
            }
        }
    }
    
    ESP_LOGI(TAG, "Dithering complete");
}

/*=============================================================================
 * Flush Callback - Convert RGB565 to E-Paper Format
 *============================================================================*/

static void epd_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p)
{
    epd_lvgl_ctx_t *ctx = lv_display_get_user_data(disp);
    if (!ctx || !ctx->epd) {
        lv_display_flush_ready(disp);
        return;
    }
    
    uint16_t *buffer = (uint16_t *)color_p;
    uint8_t *fb = epd_get_framebuffer(ctx->epd);
    
    if (!fb) {
        lv_display_flush_ready(disp);
        return;
    }
    
    bool use_floyd_steinberg = (ctx->dither_mode == EPD_DITHER_FLOYD_STEINBERG) && ctx->rgb_buf;
    // Use ordered dither when: explicitly set OR when Floyd-Steinberg requested but no RGB buffer
    bool use_ordered_dither = (ctx->dither_mode == EPD_DITHER_ORDERED) || 
                              ((ctx->dither_mode == EPD_DITHER_FLOYD_STEINBERG) && !ctx->rgb_buf);
    
    if (use_floyd_steinberg) {
        // Store RGB888 in buffer for dithering
        for (int y = area->y1; y <= area->y2; y++) {
            for (int x = area->x1; x <= area->x2; x++) {
                // RGB565 to RGB888 with proper bit expansion
                // This ensures white (31,63,31) -> (255,255,255)
                uint16_t c = *buffer++;
                uint8_t r5 = (c >> 11) & 0x1F;
                uint8_t g6 = (c >> 5) & 0x3F;
                uint8_t b5 = c & 0x1F;
                uint8_t r = (r5 << 3) | (r5 >> 2);  // 5-bit to 8-bit
                uint8_t g = (g6 << 2) | (g6 >> 4);  // 6-bit to 8-bit
                uint8_t b = (b5 << 3) | (b5 >> 2);  // 5-bit to 8-bit
                
                int idx = (y * ctx->width + x) * 3;
                ctx->rgb_buf[idx + 0] = r;
                ctx->rgb_buf[idx + 1] = g;
                ctx->rgb_buf[idx + 2] = b;
            }
            // Yield periodically
            if ((y % 50) == 0) {
                vTaskDelay(1);
            }
        }
        
        // Apply dithering after all pixels collected
        bool is_last = (area->x2 == ctx->width - 1) && (area->y2 == ctx->height - 1);
        if (is_last) {
            // Clear framebuffer first using bits_per_pixel from panel
            uint32_t fb_size = epd_calc_buffer_size(ctx->width, ctx->height, ctx->bits_per_pixel);
            uint8_t fill_byte;
            if (ctx->bits_per_pixel == 4) {
                fill_byte = (EPD_PIXEL_WHITE << 4) | EPD_PIXEL_WHITE;
            } else if (ctx->bits_per_pixel == 2) {
                fill_byte = 0x55;  // White (01) for all 4 pixels
            } else {
                fill_byte = 0xFF;  // 1-bit white
            }
            memset(fb, fill_byte, fb_size);
            
            apply_floyd_steinberg_dithering(ctx, fb);
        }
    } else {
        // Direct conversion with optional ordered (Bayer) dithering
        // Clear framebuffer only on first chunk (top-left corner)
        bool is_first = (area->x1 == 0 && area->y1 == 0);
        if (is_first) {
            uint32_t fb_size = epd_calc_buffer_size(ctx->width, ctx->height, ctx->bits_per_pixel);
            uint8_t fill_byte;
            if (ctx->bits_per_pixel == 4) {
                fill_byte = (EPD_PIXEL_WHITE << 4) | EPD_PIXEL_WHITE;
            } else if (ctx->bits_per_pixel == 2) {
                fill_byte = 0x55;  // White (01) for all 4 pixels
            } else {
                fill_byte = 0xFF;  // 1-bit white
            }
            memset(fb, fill_byte, fb_size);
        }
        
        for (int y = area->y1; y <= area->y2; y++) {
            for (int x = area->x1; x <= area->x2; x++) {
                // RGB565 to RGB888 with proper bit expansion
                uint16_t c = *buffer++;
                uint8_t r5 = (c >> 11) & 0x1F;
                uint8_t g6 = (c >> 5) & 0x3F;
                uint8_t b5 = c & 0x1F;
                uint8_t r = (r5 << 3) | (r5 >> 2);
                uint8_t g = (g6 << 2) | (g6 >> 4);
                uint8_t b = (b5 << 3) | (b5 >> 2);
                
                uint8_t epaper_color;
                
                if (use_ordered_dither) {
                    // Bayer ordered dithering (no extra buffer needed)
                    uint8_t threshold = BAYER_4X4[y % 4][x % 4];
                    
                    if (ctx->color_mode == EPD_COLOR_BW) {
                        int gray = (r * 299 + g * 587 + b * 114) / 1000;
                        epaper_color = (gray > threshold) ? EPD_PIXEL_WHITE : EPD_PIXEL_BLACK;
                    } else if (ctx->color_mode == EPD_COLOR_4COLOR) {
                        // Ordered dithering for 4-color BWRY
                        // Adjust RGB values with dither threshold for better color selection
                        int dr = r + (threshold - 128) / 2;
                        int dg = g + (threshold - 128) / 2;
                        int db = b + (threshold - 128) / 2;
                        dr = dr < 0 ? 0 : (dr > 255 ? 255 : dr);
                        dg = dg < 0 ? 0 : (dg > 255 ? 255 : dg);
                        db = db < 0 ? 0 : (db > 255 ? 255 : db);
                        epaper_color = epd_rgb_to_epaper_color(dr, dg, db, ctx->color_mode);
                    } else {
                        // Other multi-color modes
                        epaper_color = epd_rgb_to_epaper_color(r, g, b, ctx->color_mode);
                    }
                } else {
                    // Simple threshold conversion (no dithering)
                    epaper_color = epd_rgb_to_epaper_color(r, g, b, ctx->color_mode);
                }
                
                // Set pixel (format depends on color mode)
                set_fb_pixel(fb, x, y, ctx->width, epaper_color, ctx->color_mode);
            }
            // Yield periodically
            if ((y % 50) == 0) {
                vTaskDelay(1);
            }
        }
    }
    
    // Mark framebuffer as dirty
    ctx->fb_dirty = true;
    
    // Check if this is the last chunk (bottom-right corner)
    bool is_last = (area->x2 >= ctx->width - 1) && (area->y2 >= ctx->height - 1);
    
    // Only update display when:
    // 1. Full render mode (not partial) - update every flush
    // 2. Partial render mode - only update on last chunk
    if (!ctx->partial_render_mode || is_last) {
        // Determine update mode
        epd_update_mode_t mode = ctx->update_mode;
        if (ctx->force_full) {
            mode = EPD_UPDATE_FULL;
            ctx->force_full = false;
            ctx->partial_count = 0;
        }
        
        // Update display
        epd_flush_framebuffer(ctx->epd, mode);
        ctx->fb_dirty = false;
    }
    
    lv_display_flush_ready(disp);
}

/*=============================================================================
 * Public API
 *============================================================================*/

lv_display_t* epd_lvgl_init(const epd_lvgl_config_t *config)
{
    if (!config || !config->epd) {
        ESP_LOGE(TAG, "Invalid config");
        return NULL;
    }
    
    // Get panel info
    epd_panel_info_t info;
    if (epd_get_info(config->epd, &info) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get panel info");
        return NULL;
    }
    
    // Allocate context
    epd_lvgl_ctx_t *ctx = calloc(1, sizeof(epd_lvgl_ctx_t));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate context");
        return NULL;
    }
    
    ctx->epd = config->epd;
    ctx->update_mode = config->update_mode;
    ctx->dither_mode = config->dither_mode;
    ctx->color_mode = info.color_mode;
    ctx->use_partial_refresh = config->use_partial_refresh;
    ctx->partial_threshold = config->partial_threshold;
    ctx->width = info.width;
    ctx->height = info.height;
    ctx->force_full = false;
    ctx->partial_count = 0;

    // Get bits_per_pixel from panel descriptor
    const epd_panel_desc_t *panel = epd_get_panel(config->epd);
    ctx->bits_per_pixel = panel ? panel->bits_per_pixel : 1;
    
    // Calculate buffer size based on available memory
    // Full screen buffer for PSRAM, partial buffer for internal RAM only
    uint32_t full_buf_size = info.width * info.height * sizeof(uint16_t);
    bool use_partial_render = false;
    
    // Try SPIRAM first for full buffer
    ctx->lvgl_buf = heap_caps_malloc(full_buf_size, MALLOC_CAP_SPIRAM);
    if (ctx->lvgl_buf) {
        ctx->lvgl_buf_size = full_buf_size;
        ESP_LOGI(TAG, "Using SPIRAM for full buffer: %lu bytes", ctx->lvgl_buf_size);
    } else {
        // No SPIRAM - use partial rendering with smaller buffer
        // Buffer for 10 lines at a time (saves memory on no-PSRAM boards)
        uint32_t partial_lines = 10;
        uint32_t partial_buf_size = info.width * partial_lines * sizeof(uint16_t);
        
        ctx->lvgl_buf = heap_caps_malloc(partial_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!ctx->lvgl_buf) {
            ctx->lvgl_buf = malloc(partial_buf_size);
        }
        
        if (ctx->lvgl_buf) {
            ctx->lvgl_buf_size = partial_buf_size;
            ctx->partial_render_mode = true;
            use_partial_render = true;
            ESP_LOGI(TAG, "No SPIRAM, using partial render buffer: %lu bytes (%lu lines)", 
                     ctx->lvgl_buf_size, partial_lines);
        }
    }
    
    if (!ctx->lvgl_buf) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffer");
        free(ctx);
        return NULL;
    }
    
    // Allocate RGB buffer for dithering if enabled
    if (config->dither_mode == EPD_DITHER_FLOYD_STEINBERG) {
        size_t rgb_buf_size = info.width * info.height * 3;  // RGB888
        ctx->rgb_buf = heap_caps_malloc(rgb_buf_size, MALLOC_CAP_SPIRAM);
        if (!ctx->rgb_buf) {
            ctx->rgb_buf = heap_caps_malloc(rgb_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (ctx->rgb_buf) {
            memset(ctx->rgb_buf, 255, rgb_buf_size);  // White
            ESP_LOGI(TAG, "Floyd-Steinberg dithering enabled, RGB buffer: %zu bytes", rgb_buf_size);
        } else {
            ESP_LOGW(TAG, "Not enough RAM for Floyd-Steinberg, using Bayer ordered dithering");
        }
    }
    
    // Create LVGL display
    lv_display_t *disp = lv_display_create(info.width, info.height);
    if (!disp) {
        ESP_LOGE(TAG, "Failed to create display");
        if (ctx->rgb_buf) free(ctx->rgb_buf);
        free(ctx->lvgl_buf);
        free(ctx);
        return NULL;
    }
    
    lv_display_set_user_data(disp, ctx);
    lv_display_set_flush_cb(disp, epd_lvgl_flush_cb);
    
    // Use partial render mode if buffer is smaller than full screen
    lv_display_render_mode_t render_mode = use_partial_render ? 
        LV_DISPLAY_RENDER_MODE_PARTIAL : LV_DISPLAY_RENDER_MODE_FULL;
    lv_display_set_buffers(disp, ctx->lvgl_buf, NULL, ctx->lvgl_buf_size, render_mode);
    
    ESP_LOGI(TAG, "LVGL display initialized: %dx%d, dither=%d, render=%s", 
             info.width, info.height, config->dither_mode,
             use_partial_render ? "partial" : "full");
    
    return disp;
}

void epd_lvgl_deinit(lv_display_t *disp)
{
    if (!disp) return;
    
    epd_lvgl_ctx_t *ctx = lv_display_get_user_data(disp);
    if (ctx) {
        if (ctx->lvgl_buf) free(ctx->lvgl_buf);
        if (ctx->rgb_buf) free(ctx->rgb_buf);
        free(ctx);
    }
    
    lv_display_delete(disp);
}

void epd_lvgl_set_update_mode(lv_display_t *disp, epd_update_mode_t mode)
{
    if (!disp) return;
    epd_lvgl_ctx_t *ctx = lv_display_get_user_data(disp);
    if (ctx) {
        ctx->update_mode = mode;
    }
}

void epd_lvgl_set_dither_mode(lv_display_t *disp, epd_dither_mode_t mode)
{
    if (!disp) return;
    epd_lvgl_ctx_t *ctx = lv_display_get_user_data(disp);
    if (ctx) {
        ctx->dither_mode = mode;
        ESP_LOGI(TAG, "Dither mode set to %d", mode);
    }
}

void epd_lvgl_force_full_refresh(lv_display_t *disp)
{
    if (!disp) return;
    epd_lvgl_ctx_t *ctx = lv_display_get_user_data(disp);
    if (ctx) {
        ctx->force_full = true;
    }
}

void epd_lvgl_refresh(lv_display_t *disp)
{
    if (!disp) return;
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
}

epd_handle_t epd_lvgl_get_epd(lv_display_t *disp)
{
    if (!disp) return NULL;
    epd_lvgl_ctx_t *ctx = lv_display_get_user_data(disp);
    return ctx ? ctx->epd : NULL;
}
