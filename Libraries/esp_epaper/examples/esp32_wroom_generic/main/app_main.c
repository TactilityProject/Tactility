/**
 * @file app_main.c
 * @brief Generic E-Paper Example for ESP32-WROOM-32D
 * 
 * Board: Good Display ESP32-WROOM-32D Development Kit
 * Default Panel: 2.66" 152x296 BW (GDEY0266T90)
 * 
 * Features demonstrated:
 * - Floyd-Steinberg dithering for grayscale simulation
 * - Partial refresh with counter updates
 * - Gradient bars and gray level display
 * 
 * Pinout (ESP32-WROOM-32D):
 *   BUSY = GPIO13, RST = GPIO12, DC = GPIO14
 *   CS = GPIO27, SCK = GPIO18, MOSI = GPIO23
 * 
 * @see https://www.good-display.com/
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

static const char *TAG = "epaper_generic";

// LVGL configuration
#define LVGL_TICK_PERIOD_MS     5
#define LVGL_TASK_MAX_DELAY_MS  500
#define LVGL_TASK_MIN_DELAY_MS  100

static lv_obj_t *counter_label = NULL;
static int counter = 0;
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
 * @brief Create grayscale gradient bar using dithering
 */
static void create_grayscale_bar(lv_obj_t *parent, int x, int y, int w, int h, int levels)
{
    int bar_width = w / levels;
    
    for (int i = 0; i < levels; i++) {
        lv_obj_t *bar = lv_obj_create(parent);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, bar_width, h);
        lv_obj_set_pos(bar, x + i * bar_width, y);
        
        // Calculate gray level (0=black, 255=white)
        uint8_t gray = (255 * i) / (levels - 1);
        lv_obj_set_style_bg_color(bar, lv_color_make(gray, gray, gray), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
    }
}

/**
 * @brief Create demo UI for 152x296 display (portrait)
 */
static void create_demo_ui(uint16_t w, uint16_t h)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    
    int margin = 8;
    int content_w = w - margin * 2;
    
    // Title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32 E-Paper");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, margin);
    
    // Panel info
    lv_obj_t *info = lv_label_create(scr);
    char info_text[32];
    snprintf(info_text, sizeof(info_text), "%dx%d Generic", w, h);
    lv_label_set_text(info, info_text);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, margin + 20);
    
    // Grayscale demo section
    lv_obj_t *gs_label = lv_label_create(scr);
    lv_label_set_text(gs_label, "Grayscale Dithering");
    lv_obj_align(gs_label, LV_ALIGN_TOP_MID, 0, margin + 45);
    
    // 8-level grayscale bar
    create_grayscale_bar(scr, margin, margin + 65, content_w, 25, 8);
    
    // Gradient (continuous)
    lv_obj_t *gradient_box = lv_obj_create(scr);
    lv_obj_remove_style_all(gradient_box);
    lv_obj_set_size(gradient_box, content_w, 25);
    lv_obj_set_pos(gradient_box, margin, margin + 95);
    lv_obj_set_style_bg_opa(gradient_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(gradient_box, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_color(gradient_box, lv_color_white(), 0);
    lv_obj_set_style_bg_grad_dir(gradient_box, LV_GRAD_DIR_HOR, 0);
    
    // Separator line
    static lv_point_precise_t line_points[2];
    line_points[0].x = margin;
    line_points[0].y = margin + 130;
    line_points[1].x = w - margin;
    line_points[1].y = margin + 130;
    lv_obj_t *line = lv_line_create(scr);
    lv_line_set_points(line, line_points, 2);
    lv_obj_set_style_line_width(line, 1, 0);
    lv_obj_set_style_line_color(line, lv_color_black(), 0);
    
    // Counter section
    lv_obj_t *counter_title = lv_label_create(scr);
    lv_label_set_text(counter_title, "Partial Refresh");
    lv_obj_align(counter_title, LV_ALIGN_TOP_MID, 0, margin + 140);
    
    // Counter with border (below title)
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, 50, 22);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, margin + 158);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, lv_color_black(), 0);
    lv_obj_set_style_radius(box, 3, 0);
    
    counter_label = lv_label_create(scr);
    lv_label_set_text(counter_label, "0");
    lv_obj_align(counter_label, LV_ALIGN_TOP_MID, 0, margin + 161);
    
    // Gray boxes with labels
    int box_y = margin + 190;
    int box_h = 40;
    int box_w = (content_w - 3 * 4) / 4;  // 4 boxes with 4px gap
    const char *gray_labels[] = {"0%", "33%", "66%", "100%"};
    
    for (int i = 0; i < 4; i++) {
        lv_obj_t *gbox = lv_obj_create(scr);
        lv_obj_remove_style_all(gbox);
        lv_obj_set_size(gbox, box_w, box_h);
        lv_obj_set_pos(gbox, margin + i * (box_w + 4), box_y);
        
        uint8_t gray = (255 * i) / 3;  // 0, 85, 170, 255
        lv_obj_set_style_bg_color(gbox, lv_color_make(gray, gray, gray), 0);
        lv_obj_set_style_bg_opa(gbox, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(gbox, 1, 0);
        lv_obj_set_style_border_color(gbox, lv_color_black(), 0);
        
        // Label below box
        lv_obj_t *lbl = lv_label_create(scr);
        lv_label_set_text(lbl, gray_labels[i]);
        lv_obj_set_pos(lbl, margin + i * (box_w + 4) + box_w/2 - 10, box_y + box_h + 5);
    }
    
    // Footer
    lv_obj_t *footer = lv_label_create(scr);
    lv_label_set_text(footer, "esp_epaper");
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -margin);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-WROOM-32D Generic E-Paper Demo ===");
    ESP_LOGI(TAG, "Panel: 2.66\" 152x296 BW (Generic SSD16xx)");
    
    // Initialize LVGL
    lv_init();
    
    // Configure e-paper with ESP32-WROOM-32D preset
    epd_config_t epd_cfg = EPD_CONFIG_ESP32_WROOM();
    epd_handle_t epd = NULL;
    
    esp_err_t ret = epd_init(&epd_cfg, &epd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init e-paper: %s", esp_err_to_name(ret));
        return;
    }
    
    // Get panel info
    epd_panel_info_t panel_info;
    epd_get_info(epd, &panel_info);
    ESP_LOGI(TAG, "Panel: %dx%d, buffer: %lu bytes", 
             panel_info.width, panel_info.height, panel_info.buffer_size);
    
    // Initialize LVGL display with partial refresh and dithering
    epd_lvgl_config_t lvgl_cfg = EPD_LVGL_CONFIG_DEFAULT();
    lvgl_cfg.epd = epd;
    lvgl_cfg.update_mode = EPD_UPDATE_PARTIAL;
    lvgl_cfg.use_partial_refresh = true;
    lvgl_cfg.partial_threshold = 2000;  // Force full refresh every N partial updates
    lvgl_cfg.dither_mode = EPD_DITHER_FLOYD_STEINBERG;  // Enable grayscale dithering
    
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
        create_demo_ui(panel_info.width, panel_info.height);
        lvgl_unlock();
    }
    
    ESP_LOGI(TAG, "Demo started with Floyd-Steinberg dithering");
    ESP_LOGI(TAG, "Counter updates every 10 seconds");
    
    // Main loop - update counter periodically
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        counter++;
        ESP_LOGI(TAG, "Update counter: %d", counter);
        
        if (lvgl_lock(100)) {
            if (counter_label) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", counter);
                lv_label_set_text(counter_label, buf);
            }
            
            // Force full refresh every 5 updates to clear ghosting
            if (counter % 5 == 0) {
                ESP_LOGI(TAG, "Full refresh to clear ghosting");
                epd_lvgl_force_full_refresh(disp);
            }
            
            epd_lvgl_refresh(disp);
            lvgl_unlock();
        }
    }
}
