#include "Display.h"

#include <Gt911Touch.h>
#include <RgbDisplay.h>

std::shared_ptr<tt::hal::touch::TouchDevice> createTouch() {
    // Note for future changes: Reset pin is 38 and interrupt pin is 18
    // or INT = NC, schematic and other info floating around is kinda conflicting...
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
            .pclk_hz = 16'000'000,
            .h_res = 800,
            .v_res = 480,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = {
                .hsync_idle_low = false,
                .vsync_idle_low = false,
                .de_idle_high = false,
                .pclk_active_neg = true,
                .pclk_idle_high = false
            }
        },
        .data_width = 16,
        .bits_per_pixel = 0,
        .num_fbs = 2,
        .bounce_buffer_size_px = bufferPixels,
        .sram_trans_align = 8,
        .psram_trans_align = 64,
        .hsync_gpio_num = GPIO_NUM_46,
        .vsync_gpio_num = GPIO_NUM_3,
        .de_gpio_num = GPIO_NUM_5,
        .pclk_gpio_num = GPIO_NUM_7,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums = {
            GPIO_NUM_14, // B3
            GPIO_NUM_38, // B4
            GPIO_NUM_18, // B5
            GPIO_NUM_17, // B6
            GPIO_NUM_10, // B7
            GPIO_NUM_39, // G2
            GPIO_NUM_0, // G3
            GPIO_NUM_45, // G4
            GPIO_NUM_48, // G5
            GPIO_NUM_47, // G6
            GPIO_NUM_21, // G7
            GPIO_NUM_1, // R3
            GPIO_NUM_2, // R4
            GPIO_NUM_42, // R5
            GPIO_NUM_41, // R6
            GPIO_NUM_40, // R7
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
        false
    );

    return std::make_shared<RgbDisplay>(std::move(configuration));
}
