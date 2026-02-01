#include "Display.h"

#include <Gt911Touch.h>
#include <PwmBacklight.h>
#include <RgbDisplay.h>

std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    // Note for future changes: Reset pin is 41 and interrupt pin is 40
    auto configuration = std::make_unique<Gt911Touch::Configuration>(
        I2C_NUM_0,
        800,
        480
    );

    return std::make_shared<Gt911Touch>(std::move(configuration));
}

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay() {
    auto touch = createTouch();

    constexpr uint32_t bufferPixels = 800 * 10;

    esp_lcd_rgb_panel_config_t rgb_panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 14000000,
            .h_res = 800,
            .v_res = 480,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 16,
            .vsync_front_porch = 16,
            .flags = {
                .hsync_idle_low = false,
                .vsync_idle_low = false,
                .de_idle_high = false,
                .pclk_active_neg = true,
                .pclk_idle_high = true
            }
        },
        .data_width = 16,
        .bits_per_pixel = 0,
        .num_fbs = 2,
        .bounce_buffer_size_px = bufferPixels,
        .sram_trans_align = 8,
        .psram_trans_align = 64,
        .hsync_gpio_num = GPIO_NUM_NC,
        .vsync_gpio_num = GPIO_NUM_NC,
        .de_gpio_num = GPIO_NUM_38,
        .pclk_gpio_num = GPIO_NUM_5,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums = {
            GPIO_NUM_17, // B
            GPIO_NUM_18, // B
            GPIO_NUM_48, // B
            GPIO_NUM_47, // B
            GPIO_NUM_39, // B
            GPIO_NUM_11, // G
            GPIO_NUM_12, // G
            GPIO_NUM_13, // G
            GPIO_NUM_14, // G
            GPIO_NUM_15, // G
            GPIO_NUM_16, // G
            GPIO_NUM_6, // R
            GPIO_NUM_7, // R
            GPIO_NUM_8, // R
            GPIO_NUM_9, // R
            GPIO_NUM_10, // R
        },
        .flags = {
            .disp_active_low = false,
            .refresh_on_demand = false,
            .fb_in_psram = true,
            .double_fb = true,
            .no_fb = false,
            .bb_invalidate_cache = false
        }
    };

    RgbDisplay::BufferConfiguration buffer_config = {
        .size = (800 * 480),
        .useSpi = true,
        .doubleBuffer = true,
        .bounceBufferMode = true,
        .avoidTearing = false
    };

    auto configuration = std::make_unique<RgbDisplay::Configuration>(
        rgb_panel_config,
        buffer_config,
        touch,
        LV_COLOR_FORMAT_RGB565,
        false,
        false,
        false,
        false,
        driver::pwmbacklight::setBacklightDuty
    );

    return std::make_shared<RgbDisplay>(std::move(configuration));
}