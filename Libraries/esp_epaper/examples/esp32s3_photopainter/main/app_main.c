/**
 * @file app_main.c
 * @brief Example for Waveshare ESP32-S3-PhotoPainter
 * 
 * Board: Waveshare ESP32-S3-PhotoPainter
 * Panel: GDEP073E01 (800x480 6-Color)
 * Colors: Black, White, Yellow, Red, Blue, Green
 * Features: Floyd-Steinberg dithering for photo-quality images
 * 
 * @see https://www.waveshare.com/wiki/ESP32-S3-PhotoPainter
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "epaper.h"
#include "epaper_lvgl.h"
#include "lvgl.h"

static const char *TAG = "photopainter";

// LVGL configuration
#define LVGL_TICK_PERIOD_MS     5
#define LVGL_TASK_MAX_DELAY_MS  500
#define LVGL_TASK_MIN_DELAY_MS  100

static SemaphoreHandle_t lvgl_mux = NULL;

static void lv_tick_timer_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static bool lvgl_lock(int timeout_ms)
{
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

static void lvgl_unlock(void)
{
    xSemaphoreGive(lvgl_mux);
}

static void lv_handler_task(void *arg)
{
    uint32_t task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        if (lvgl_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            lvgl_unlock();
        }
        if (task_delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

/**
 * @brief Create colorful demo UI for 800x480 6-color display
 * 
 * Demonstrates:
 * - 6 color palette
 * - Grayscale gradient (dithered)
 * - Color gradient (dithered)
 */
static void create_demo_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    
    // Title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32-S3-PhotoPainter Demo");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Subtitle
    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "GDEP073E01 - 800x480 - 6 Colors");
    lv_obj_set_style_text_color(subtitle, lv_color_make(100, 100, 100), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 35);
    
    // 6-Color palette demonstration
    const struct {
        const char *name;
        lv_color_t color;
        int x_offset;
    } colors[] = {
        {"Black",  lv_color_make(0, 0, 0),       -300},
        {"White",  lv_color_make(255, 255, 255), -180},
        {"Yellow", lv_color_make(255, 255, 0),   -60},
        {"Red",    lv_color_make(255, 0, 0),     60},
        {"Blue",   lv_color_make(0, 0, 255),     180},
        {"Green",  lv_color_make(0, 255, 0),     300},
    };
    
    for (int i = 0; i < 6; i++) {
        // Color box
        lv_obj_t *box = lv_obj_create(scr);
        lv_obj_set_size(box, 100, 80);
        lv_obj_align(box, LV_ALIGN_TOP_MID, colors[i].x_offset, 70);
        lv_obj_set_style_bg_color(box, colors[i].color, 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_border_color(box, lv_color_black(), 0);
        lv_obj_set_style_radius(box, 8, 0);
        
        // Color label
        lv_obj_t *label = lv_label_create(scr);
        lv_label_set_text(label, colors[i].name);
        lv_obj_set_style_text_color(label, lv_color_black(), 0);
        lv_obj_align(label, LV_ALIGN_TOP_MID, colors[i].x_offset, 155);
    }
    
    // Grayscale gradient (demonstrates dithering)
    lv_obj_t *gradient_label = lv_label_create(scr);
    lv_label_set_text(gradient_label, "Grayscale Gradient (Floyd-Steinberg dithered):");
    lv_obj_set_style_text_color(gradient_label, lv_color_black(), 0);
    lv_obj_align(gradient_label, LV_ALIGN_TOP_LEFT, 30, 190);
    
    for (int i = 0; i < 16; i++) {
        lv_obj_t *grad_box = lv_obj_create(scr);
        lv_obj_set_size(grad_box, 45, 50);
        lv_obj_align(grad_box, LV_ALIGN_TOP_LEFT, 30 + i * 47, 215);
        uint8_t gray = 255 - (i * 17);
        lv_obj_set_style_bg_color(grad_box, lv_color_make(gray, gray, gray), 0);
        lv_obj_set_style_border_width(grad_box, 0, 0);
        lv_obj_set_style_radius(grad_box, 0, 0);
    }
    
    // Color gradient
    lv_obj_t *color_grad_label = lv_label_create(scr);
    lv_label_set_text(color_grad_label, "Color Gradients:");
    lv_obj_set_style_text_color(color_grad_label, lv_color_black(), 0);
    lv_obj_align(color_grad_label, LV_ALIGN_TOP_LEFT, 30, 280);
    
    // Red to Yellow gradient
    for (int i = 0; i < 8; i++) {
        lv_obj_t *box = lv_obj_create(scr);
        lv_obj_set_size(box, 45, 50);
        lv_obj_align(box, LV_ALIGN_TOP_LEFT, 30 + i * 47, 305);
        uint8_t g = i * 36;
        lv_obj_set_style_bg_color(box, lv_color_make(255, g, 0), 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_set_style_radius(box, 0, 0);
    }
    
    // Blue to Green gradient
    for (int i = 0; i < 8; i++) {
        lv_obj_t *box = lv_obj_create(scr);
        lv_obj_set_size(box, 45, 50);
        lv_obj_align(box, LV_ALIGN_TOP_LEFT, 406 + i * 47, 305);
        uint8_t g = i * 36;
        uint8_t b = 255 - i * 36;
        lv_obj_set_style_bg_color(box, lv_color_make(0, g, b), 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_set_style_radius(box, 0, 0);
    }
    
    // Info panel
    lv_obj_t *info_panel = lv_obj_create(scr);
    lv_obj_set_size(info_panel, 760, 90);
    lv_obj_align(info_panel, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(info_panel, lv_color_make(245, 245, 245), 0);
    lv_obj_set_style_border_width(info_panel, 1, 0);
    lv_obj_set_style_radius(info_panel, 8, 0);
    
    lv_obj_t *info_text = lv_label_create(info_panel);
    lv_label_set_text(info_text, 
        "6 colors: Black, White, Yellow, Red, Blue, Green\n"
        "Floyd-Steinberg dithering enabled | Full refresh ~20 seconds");
    lv_obj_set_style_text_color(info_text, lv_color_black(), 0);
    lv_obj_set_style_text_align(info_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(info_text);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Waveshare ESP32-S3-PhotoPainter Demo ===");
    ESP_LOGI(TAG, "Panel: GDEP073E01 (800x480 6-Color)");
    
    // Initialize LVGL
    lv_init();
    
    // Configure e-paper with preset for PhotoPainter (7.3" 6-color)
    epd_config_t epd_cfg = EPD_CONFIG_73_6COLOR();
    epd_handle_t epd = NULL;
    
    esp_err_t ret = epd_init(&epd_cfg, &epd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init e-paper: %s", esp_err_to_name(ret));
        return;
    }
    
    // Get panel info
    epd_panel_info_t panel_info;
    epd_get_info(epd, &panel_info);
    ESP_LOGI(TAG, "Panel: %dx%d, buffer: %lu bytes, color_mode: %d", 
             panel_info.width, panel_info.height, 
             panel_info.buffer_size, panel_info.color_mode);
    
    // Initialize LVGL display with dithering enabled
    epd_lvgl_config_t lvgl_cfg = EPD_LVGL_CONFIG_DEFAULT();
    lvgl_cfg.epd = epd;
    lvgl_cfg.update_mode = EPD_UPDATE_FULL;  // 6-color only supports full refresh
    lvgl_cfg.dither_mode = EPD_DITHER_FLOYD_STEINBERG;  // Enable dithering for gradients
    
    lv_display_t *disp = epd_lvgl_init(&lvgl_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "Failed to init LVGL display");
        epd_deinit(epd);
        return;
    }
    
    // Setup LVGL tick timer
    esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lv_tick_timer_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));
    
    // Create LVGL task (larger stack for dithering)
    lvgl_mux = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(lv_handler_task, "LVGL", 16 * 1024, NULL, 4, NULL, 1);
    
    // Create UI
    if (lvgl_lock(-1)) {
        create_demo_ui();
        lvgl_unlock();
    }
    
    ESP_LOGI(TAG, "Waiting 1 second before refresh...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Refresh display
    if (lvgl_lock(-1)) {
        ESP_LOGI(TAG, "Refreshing display (this takes ~20 seconds)...");
        epd_lvgl_refresh(disp);
        lvgl_unlock();
    }
    
    ESP_LOGI(TAG, "Display refresh complete!");
    
    // Main loop - keep alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        ESP_LOGI(TAG, "System running... (refresh takes ~20s, no partial refresh on 6-color panels)");
    }
}
