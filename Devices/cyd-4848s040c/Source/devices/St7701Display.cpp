#include "St7701Display.h"

#include <Gt911Touch.h>
#include <PwmBacklight.h>
#include <Tactility/Logger.h>

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_io_additions.h>
#include <esp_lcd_st7701.h>

static const auto LOGGER = tt::Logger("St7701Display");

static const st7701_lcd_init_cmd_t st7701_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xFF, (uint8_t[]) {0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]) {0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t[]) {0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t[]) {0x31, 0x05}, 2, 0},
    {0xCD, (uint8_t[]) {0x00}, 1, 0}, //0x08
    //Positive Voltage Gamma Control
    {0xB0, (uint8_t[]) {0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}, 16, 0},
    //Negative Voltage Gamma Control
    {0xB1, (uint8_t[]) {0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}, 16, 0},
    //Page1
    {0xFF, (uint8_t[]) {0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]) {0x60}, 1, 0}, //Vop=4.7375v
    {0xB1, (uint8_t[]) {0x32}, 1, 0}, //VCOM=32
    {0xB2, (uint8_t[]) {0x07}, 1, 0}, //VGH=15v
    {0xB3, (uint8_t[]) {0x80}, 1, 0},
    {0xB5, (uint8_t[]) {0x49}, 1, 0}, //VGL=-10.17v
    {0xB7, (uint8_t[]) {0x85}, 1, 0},
    {0xB8, (uint8_t[]) {0x21}, 1, 0}, //AVDD=6.6 & AVCL=-4.6
    {0xC1, (uint8_t[]) {0x78}, 1, 0},
    {0xC2, (uint8_t[]) {0x78}, 1, 0},
    {0xE0, (uint8_t[]) {0x00, 0x1B, 0x02}, 3, 0},
    {0xE1, (uint8_t[]) {0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44}, 11, 0},
    {0xE2, (uint8_t[]) {0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t[]) {0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, (uint8_t[]) {0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t[]) {0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0}, 16, 0},
    {0xE6, (uint8_t[]) {0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, (uint8_t[]) {0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t[]) {0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0}, 16, 0},
    {0xEB, (uint8_t[]) {0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40}, 7, 0},
    {0xEC, (uint8_t[]) {0x3C, 0x00}, 2, 0},
    {0xED, (uint8_t[]) {0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}, 16, 0},
    //VAP & VAN
    {0xFF, (uint8_t[]) {0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xE5, (uint8_t[]) {0xE4}, 1, 0},
    {0xFF, (uint8_t[]) {0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x3A, (uint8_t[]) {0x60}, 1, 10}, //0x70 RGB888, 0x60 RGB666, 0x50 RGB565
    {0x11, (uint8_t[]) {0x00}, 0, 120}, //Sleep Out
    {0x29, (uint8_t[]) {0x00}, 0, 0}, //Display On
};

bool St7701Display::createIoHandle(esp_lcd_panel_io_handle_t& outHandle) {
    spi_line_config_t line_config = {
        .cs_io_type = IO_TYPE_GPIO,
        .cs_gpio_num = GPIO_NUM_39,
        .scl_io_type = IO_TYPE_GPIO,
        .scl_gpio_num = GPIO_NUM_48,
        .sda_io_type = IO_TYPE_GPIO,
        .sda_gpio_num = GPIO_NUM_47,
        .io_expander = nullptr,
    };
    esp_lcd_panel_io_3wire_spi_config_t panel_io_config = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line_config, 0);
    return esp_lcd_new_panel_io_3wire_spi(&panel_io_config, &outHandle) == ESP_OK;
}

bool St7701Display::createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t& panelHandle) {
    const esp_lcd_rgb_panel_config_t rgb_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 14000000,
            .h_res = 480,
            .v_res = 480,
            .hsync_pulse_width = 10,
            .hsync_back_porch = 10,
            .hsync_front_porch = 20,
            .vsync_pulse_width = 10,
            .vsync_back_porch = 10,
            .vsync_front_porch = 10,
            .flags = {
                .hsync_idle_low = false,
                .vsync_idle_low = false,
                .de_idle_high = false,
                .pclk_active_neg = false,
                .pclk_idle_high = false
            }
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,
        .bounce_buffer_size_px = 480 * 10,
        .sram_trans_align = 8,
        .psram_trans_align = 64,
        .hsync_gpio_num = GPIO_NUM_16,
        .vsync_gpio_num = GPIO_NUM_17,
        .de_gpio_num = GPIO_NUM_18,
        .pclk_gpio_num = GPIO_NUM_21,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums = {
            GPIO_NUM_4, // B1
            GPIO_NUM_5, // B2
            GPIO_NUM_6, // B3
            GPIO_NUM_7, // B4
            GPIO_NUM_15, // B5
            GPIO_NUM_8, // G1
            GPIO_NUM_20, // G2
            GPIO_NUM_3, // G3
            GPIO_NUM_46, // G4
            GPIO_NUM_9, // G5
            GPIO_NUM_10, // G6
            GPIO_NUM_11, // R1
            GPIO_NUM_12, // R2
            GPIO_NUM_13, // R3
            GPIO_NUM_14, // R4
            GPIO_NUM_0 // R5
        },
        .flags = {
            .disp_active_low = false,
            .refresh_on_demand = false,
            .fb_in_psram = true,
            .double_fb = true,
            .no_fb = false,
            .bb_invalidate_cache = true
        }
    };

    st7701_vendor_config_t vendor_config = {
        .init_cmds = st7701_lcd_init_cmds,
        .init_cmds_size = sizeof(st7701_lcd_init_cmds) / sizeof(st7701_lcd_init_cmd_t),
        .rgb_config = &rgb_config,
        .flags = {
            .use_mipi_interface = 0,
            .mirror_by_cmd = 1,
            .auto_del_panel_io = 0,
        },
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = false,
        },
        .vendor_config = &vendor_config,
    };

    if (esp_lcd_new_panel_st7701(ioHandle, &panel_config, &panelHandle) != ESP_OK) {
        LOGGER.error("Failed to create panel");
        return false;
    }

    if (esp_lcd_panel_reset(panelHandle) != ESP_OK) {
        LOGGER.error("Failed to reset panel");
        return false;
    }

    if (esp_lcd_panel_init(panelHandle) != ESP_OK) {
        LOGGER.error("Failed to init panel");
        return false;
    }

    if (esp_lcd_panel_invert_color(panelHandle, false) != ESP_OK) {
        LOGGER.error("Failed to invert color");
        return false;
    }

    esp_lcd_panel_set_gap(panelHandle, 0, 0);

    if (esp_lcd_panel_disp_on_off(panelHandle, true) != ESP_OK) {
        LOGGER.error("Failed to turn display on");
        return false;
    }

    return true;
}

lvgl_port_display_cfg_t St7701Display::getLvglPortDisplayConfig(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle) {
    return {
        .io_handle = ioHandle,
        .panel_handle = panelHandle,
        .control_handle = nullptr,
        .buffer_size = (480 * 480),
        .double_buffer = true,
        .trans_size = 0,
        .hres = 480,
        .vres = 480,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
            .swap_bytes = false,
            .full_refresh = false,
            .direct_mode = false
        }
    };
}

lvgl_port_display_rgb_cfg_t St7701Display::getLvglPortDisplayRgbConfig(esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle) {
    return {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = false
        }
    };
}

std::shared_ptr<tt::hal::touch::TouchDevice> St7701Display::getTouchDevice() {
    if (touchDevice == nullptr) {
        auto configuration = std::make_unique<Gt911Touch::Configuration>(
           I2C_NUM_0,
           480,
           480
       );

        touchDevice = std::make_shared<Gt911Touch>(std::move(configuration));
    }

    return touchDevice;
}

void St7701Display::setBacklightDuty(uint8_t backlightDuty) {
    driver::pwmbacklight::setBacklightDuty(backlightDuty);
}
