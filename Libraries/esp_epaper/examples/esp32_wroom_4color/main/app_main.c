/**
 * @file app_main.c
 * @brief Example for ESP32-WROOM-32D + GDEY037F51 4-Color E-Paper
 * 
 * Board: Good Display ESP32-WROOM-32D Development Kit (No PSRAM)
 * Panel: GDEY037F51 (240x416 4-Color BWRY)
 * Colors: Black, White, Yellow, Red
 * 
 * Pinout:
 *   BUSY = GPIO13, RST = GPIO12, DC = GPIO14
 *   CS = GPIO27, SCK = GPIO18, MOSI = GPIO23
 * 
 * @see https://www.good-display.com/product/505.html
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "epaper.h"
#include "epaper_lvgl.h"
#include "lvgl.h"

static const char *TAG = "epd_4color";

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
 * @brief Create demo UI for 240x416 4-color display
 * 
 * Demonstrates:
 * - 4 color palette (BWRY)
 * - Color patterns
 * - Panel information
 */
static void create_demo_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    
    // Title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32 4-Color Demo");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Subtitle
    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "GDEY037F51 - 240x416");
    lv_obj_set_style_text_color(subtitle, lv_color_make(100, 100, 100), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 30);
    
    // 4-Color palette demonstration
    const struct {
        const char *name;
        lv_color_t color;
    } colors[] = {
        {"B", lv_color_make(0, 0, 0)},         // Black
        {"W", lv_color_make(255, 255, 255)},   // White
        {"Y", lv_color_make(255, 255, 0)},     // Yellow
        {"R", lv_color_make(255, 0, 0)},       // Red
    };
    
    int box_w = 50;
    int box_h = 50;
    int start_x = 20;
    int gap = 5;
    
    for (int i = 0; i < 4; i++) {
        // Color box
        lv_obj_t *box = lv_obj_create(scr);
        lv_obj_set_size(box, box_w, box_h);
        lv_obj_set_pos(box, start_x + i * (box_w + gap), 55);
        lv_obj_set_style_bg_color(box, colors[i].color, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_border_color(box, lv_color_black(), 0);
        lv_obj_set_style_radius(box, 4, 0);
        
        // Color label inside box
        lv_obj_t *label = lv_label_create(box);
        lv_label_set_text(label, colors[i].name);
        lv_obj_center(label);
        // Contrast text color
        if (i == 0) {  // Black box
            lv_obj_set_style_text_color(label, lv_color_white(), 0);
        } else {
            lv_obj_set_style_text_color(label, lv_color_black(), 0);
        }
    }
    
    // Color Gradients section (demonstrates dithering)
    lv_obj_t *grad_label = lv_label_create(scr);
    lv_label_set_text(grad_label, "Gradients (dithered):");
    lv_obj_set_style_text_color(grad_label, lv_color_black(), 0);
    lv_obj_set_pos(grad_label, 10, 115);
    
    int grad_w = 24;
    int grad_h = 32;
    int grad_steps = 8;
    
    // Grayscale: Black -> White
    for (int i = 0; i < grad_steps; i++) {
        lv_obj_t *box = lv_obj_create(scr);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, grad_w, grad_h);
        lv_obj_set_pos(box, 10 + i * grad_w, 135);
        uint8_t gray = (i * 255) / (grad_steps - 1);
        lv_obj_set_style_bg_color(box, lv_color_make(gray, gray, gray), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    }
    
    // Red -> Yellow gradient
    for (int i = 0; i < grad_steps; i++) {
        lv_obj_t *box = lv_obj_create(scr);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, grad_w, grad_h);
        lv_obj_set_pos(box, 10 + i * grad_w, 170);
        uint8_t g = (i * 255) / (grad_steps - 1);
        lv_obj_set_style_bg_color(box, lv_color_make(255, g, 0), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    }
    
    // Yellow -> White gradient
    for (int i = 0; i < grad_steps; i++) {
        lv_obj_t *box = lv_obj_create(scr);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, grad_w, grad_h);
        lv_obj_set_pos(box, 10 + i * grad_w, 205);
        uint8_t b = (i * 255) / (grad_steps - 1);
        lv_obj_set_style_bg_color(box, lv_color_make(255, 255, b), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    }
    
    // Red -> Black gradient
    for (int i = 0; i < grad_steps; i++) {
        lv_obj_t *box = lv_obj_create(scr);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, grad_w, grad_h);
        lv_obj_set_pos(box, 10 + i * grad_w, 240);
        uint8_t r = 255 - (i * 255) / (grad_steps - 1);
        lv_obj_set_style_bg_color(box, lv_color_make(r, 0, 0), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    }
    
    // Gradient labels (positioned after gradient boxes)
    int label_x = 10 + grad_steps * grad_w + 5;
    
    lv_obj_t *lbl1 = lv_label_create(scr);
    lv_label_set_text(lbl1, "B-W");
    lv_obj_set_style_text_color(lbl1, lv_color_black(), 0);
    lv_obj_set_pos(lbl1, label_x, 142);
    
    lv_obj_t *lbl2 = lv_label_create(scr);
    lv_label_set_text(lbl2, "R-Y");
    lv_obj_set_style_text_color(lbl2, lv_color_black(), 0);
    lv_obj_set_pos(lbl2, label_x, 177);
    
    lv_obj_t *lbl3 = lv_label_create(scr);
    lv_label_set_text(lbl3, "Y-W");
    lv_obj_set_style_text_color(lbl3, lv_color_black(), 0);
    lv_obj_set_pos(lbl3, label_x, 212);
    
    lv_obj_t *lbl4 = lv_label_create(scr);
    lv_label_set_text(lbl4, "R-B");
    lv_obj_set_style_text_color(lbl4, lv_color_black(), 0);
    lv_obj_set_pos(lbl4, label_x, 247);
    
    // Info panel
    lv_obj_t *info_panel = lv_obj_create(scr);
    lv_obj_set_size(info_panel, 220, 50);
    lv_obj_align(info_panel, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(info_panel, lv_color_make(255, 255, 0), 0);  // Yellow background
    lv_obj_set_style_bg_opa(info_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(info_panel, 2, 0);
    lv_obj_set_style_border_color(info_panel, lv_color_black(), 0);
    lv_obj_set_style_radius(info_panel, 4, 0);
    
    lv_obj_t *info_text = lv_label_create(info_panel);
    lv_label_set_text(info_text, 
        "4 colors | Ordered dither\n"
        "No PSRAM required");
    lv_obj_set_style_text_color(info_text, lv_color_black(), 0);
    lv_obj_set_style_text_align(info_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(info_text);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-WROOM-32D 4-Color E-Paper Demo ===");
    ESP_LOGI(TAG, "Panel: GDEY037F51 (240x416 4-Color BWRY)");
    ESP_LOGI(TAG, "No PSRAM required");
    
    // Initialize LVGL
    lv_init();
    
    // Configure e-paper with preset for 4-color panel
    epd_config_t epd_cfg = EPD_CONFIG_ESP32_WROOM_4COLOR();
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
    
    // Initialize LVGL display with ordered dithering (no extra RAM needed)
    epd_lvgl_config_t lvgl_cfg = EPD_LVGL_CONFIG_DEFAULT();
    lvgl_cfg.epd = epd;
    lvgl_cfg.update_mode = EPD_UPDATE_FULL;  // 4-color only supports full refresh
    lvgl_cfg.dither_mode = EPD_DITHER_ORDERED;  // Ordered dithering - no extra buffer needed
    
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
    
    // Create LVGL task
    lvgl_mux = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(lv_handler_task, "LVGL", 8 * 1024, NULL, 4, NULL, 1);
    
    // Create UI
    if (lvgl_lock(-1)) {
        create_demo_ui();
        lvgl_unlock();
    }
    
    ESP_LOGI(TAG, "Waiting 1 second before refresh...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Refresh display
    if (lvgl_lock(-1)) {
        ESP_LOGI(TAG, "Refreshing display (this takes ~15 seconds)...");
        epd_lvgl_refresh(disp);
        lvgl_unlock();
    }
    
    ESP_LOGI(TAG, "Display refresh complete!");
    
    // Main loop - keep alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        ESP_LOGI(TAG, "System running...");
    }
}
