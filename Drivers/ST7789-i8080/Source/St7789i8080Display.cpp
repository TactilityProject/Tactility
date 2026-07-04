#include "St7789i8080Display.h"
#include <tactility/log.h>
#include <driver/gpio.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

constexpr auto* TAG = "St7789i8080Display";
static St7789i8080Display* g_display_instance = nullptr;

// ST7789 initialization commands
typedef struct {
    uint8_t cmd;
    uint8_t data[14];
    uint8_t len;
} lcd_init_cmd_t;

static const lcd_init_cmd_t st7789_init_cmds[] = {
    {0x11, {0}, 0 | 0x80},
    {0x36, {0x08}, 1},  
    {0x3A, {0X05}, 1},
    {0x20, {0}, 0},  
    {0xB2, {0X0B, 0X0B, 0X00, 0X33, 0X33}, 5},
    {0xB7, {0X75}, 1},
    {0xBB, {0X28}, 1},
    {0xC0, {0X2C}, 1},
    {0xC2, {0X01}, 1},
    {0xC3, {0X1F}, 1},
    {0xC6, {0X13}, 1},
    {0xD0, {0XA7}, 1},
    {0xD0, {0XA4, 0XA1}, 2},
    {0xD6, {0XA1}, 1},
    {0xE0, {0XF0, 0X05, 0X0A, 0X06, 0X06, 0X03, 0X2B, 0X32, 0X43, 0X36, 0X11, 0X10, 0X2B, 0X32}, 14},
    {0xE1, {0XF0, 0X08, 0X0C, 0X0B, 0X09, 0X24, 0X2B, 0X22, 0X43, 0X38, 0X15, 0X16, 0X2F, 0X37}, 14},
};

// Callback when color transfer is done
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, 
                                    esp_lcd_panel_io_event_data_t *edata, 
                                    void *user_ctx) {
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

St7789i8080Display::St7789i8080Display(const Configuration& config) 
    : configuration(config), lock(std::make_shared<std::mutex>()) {
    
    // Validate configuration
    if (!configuration.isValid()) {
        LOG_E(TAG, "Invalid configuration: resolution must be set");
        return;
    }
}

bool St7789i8080Display::createI80Bus() {
    LOG_I(TAG, "Creating I80 bus");
    
    // Create I80 bus configuration
    esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num = configuration.dcPin,
        .wr_gpio_num = configuration.wrPin,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            configuration.dataPins[0], configuration.dataPins[1], 
            configuration.dataPins[2], configuration.dataPins[3],
            configuration.dataPins[4], configuration.dataPins[5], 
            configuration.dataPins[6], configuration.dataPins[7],
        },
        .bus_width = configuration.busWidth,
        .max_transfer_bytes = configuration.bufferSize * sizeof(uint16_t),
        .psram_trans_align = 64,
        .sram_trans_align = 4
    };
    
    esp_err_t ret = esp_lcd_new_i80_bus(&bus_cfg, &i80BusHandle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create I80 bus: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

bool St7789i8080Display::createPanelIO() {
    LOG_I(TAG, "Creating panel IO");
    
    // Create panel IO with proper callback
    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = configuration.csPin,
        .pclk_hz = configuration.pixelClockFrequency,
        .trans_queue_depth = configuration.transactionQueueDepth,
        .on_color_trans_done = notify_lvgl_flush_ready,  // Use proper callback
        .user_ctx = nullptr,  // Will be set when LVGL display is created
        .lcd_cmd_bits = configuration.lcdCmdBits,
        .lcd_param_bits = configuration.lcdParamBits,
        .dc_levels = {
            .dc_idle_level = configuration.dcLevels.dcIdleLevel,
            .dc_cmd_level = configuration.dcLevels.dcCmdLevel,
            .dc_dummy_level = configuration.dcLevels.dcDummyLevel,
            .dc_data_level = configuration.dcLevels.dcDataLevel,
        },
        .flags = {
            .cs_active_high = configuration.flags.csActiveHigh,
            .reverse_color_bits = configuration.flags.reverseColorBits,
            .swap_color_bytes = configuration.flags.swapColorBytes,
            .pclk_active_neg = configuration.flags.pclkActiveNeg,
            .pclk_idle_low = configuration.flags.pclkIdleLow
        }
    };

    esp_err_t ret = esp_lcd_new_panel_io_i80(i80BusHandle, &io_cfg, &ioHandle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

bool St7789i8080Display::createPanel() {
    LOG_I(TAG, "Configuring panel");
    
    // Create ST7789 panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = configuration.resetPin,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = false
        },
        .vendor_config = nullptr
    };
    
    esp_err_t ret = esp_lcd_new_panel_st7789(ioHandle, &panel_config, &panelHandle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create ST7789 panel: %s", esp_err_to_name(ret));
        return false;
    }

    // Reset panel
    ret = esp_lcd_panel_reset(panelHandle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to reset panel: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize panel
    ret = esp_lcd_panel_init(panelHandle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to init panel: %s", esp_err_to_name(ret));
        return false;
    }

    // Set gap
    ret = esp_lcd_panel_set_gap(panelHandle, configuration.gapX, configuration.gapY);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to set panel gap: %s", esp_err_to_name(ret));
        return false;
    }

    // Set inversion
    ret = esp_lcd_panel_invert_color(panelHandle, configuration.invertColor);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to set panel inversion: %s", esp_err_to_name(ret));
        return false;
    }

    // Set mirror
    ret = esp_lcd_panel_mirror(panelHandle, configuration.mirrorX, configuration.mirrorY);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to set panel mirror: %s", esp_err_to_name(ret));
        return false;
    }

    // Turn on display
    ret = esp_lcd_panel_disp_on_off(panelHandle, true);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to turn display on: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

void St7789i8080Display::sendInitCommands() {
    LOG_I(TAG, "Sending ST7789 init commands");
    for (const auto& cmd : st7789_init_cmds) {
        esp_lcd_panel_io_tx_param(ioHandle, cmd.cmd, cmd.data, cmd.len & 0x7F);
        if (cmd.len & 0x80) {
            vTaskDelay(pdMS_TO_TICKS(120));
        }
    }
}

bool St7789i8080Display::start() {
    LOG_I(TAG, "Initializing I8080 ST7789 Display hardware...");

    // Configure RD pin if needed
    if (configuration.rdPin != GPIO_NUM_NC) {
        gpio_config_t rd_gpio_config = {
            .pin_bit_mask = (1ULL << static_cast<uint32_t>(configuration.rdPin)),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&rd_gpio_config);
        gpio_set_level(configuration.rdPin, 1);
    }

    // Calculate buffer size if needed
    configuration.calculateBufferSize();
    
    // Allocate buffer based on resolution
    size_t buffer_size = configuration.bufferSize * LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565);
    buf1 = (uint8_t*)heap_caps_malloc(buffer_size, MALLOC_CAP_DMA);
    if (!buf1) {
        LOG_E(TAG, "Failed to allocate display buffer");
        return false;
    }

    // Create I80 bus
    if (!createI80Bus()) {
        return false;
    }
    
    // Create panel IO
    if (!createPanelIO()) {
        return false;
    }
    
    // Create panel
    if (!createPanel()) {
        return false;
    }

    LOG_I(TAG, "Display hardware initialized");
    return true;
}

bool St7789i8080Display::stop() {
    // Turn off display
    if (panelHandle) {
        esp_lcd_panel_disp_on_off(panelHandle, false);
    }

    // Destroy in reverse order: panel, IO, bus
    if (panelHandle) {
        esp_lcd_panel_del(panelHandle);
        panelHandle = nullptr;
    }
    if (ioHandle) {
        esp_lcd_panel_io_del(ioHandle);
        ioHandle = nullptr;
    }
    if (i80BusHandle) {
        esp_lcd_del_i80_bus(i80BusHandle);
        i80BusHandle = nullptr;
    }

    // Free buffer
    if (buf1) {
        heap_caps_free(buf1);
        buf1 = nullptr;
    }

    // Turn off backlight
    if (configuration.backlightDutyFunction) {
        configuration.backlightDutyFunction(0);
    }

    return true;
}

bool St7789i8080Display::startLvgl() {
    LOG_I(TAG, "Initializing LVGL for ST7789 display");

    // Don't reinitialize hardware if it's already done
    if (!ioHandle) {
        LOG_I(TAG, "Hardware not initialized, calling start()");
        if (!start()) {
            LOG_E(TAG, "Hardware initialization failed");
            return false;
        }
    } else {
        LOG_I(TAG, "Hardware already initialized, skipping");
    }

    // Create LVGL display using lvgl_port
    lvgl_port_display_cfg_t display_cfg = {
        .io_handle = ioHandle,
        .panel_handle = panelHandle,
        .control_handle = nullptr,
        .buffer_size = configuration.bufferSize,
        .double_buffer = false,
        .trans_size = 0,
        .hres = configuration.horizontalResolution,
        .vres = configuration.verticalResolution,
        .monochrome = false,
        .rotation = {
            .swap_xy = configuration.swapXY,
            .mirror_x = configuration.mirrorX,
            .mirror_y = configuration.mirrorY,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = true,
            .swap_bytes = true,
            .full_refresh = false,
            .direct_mode = false
        }
    };

    // Create the LVGL display
    lvglDisplay = lvgl_port_add_disp(&display_cfg);
    if (!lvglDisplay) {
        LOG_E(TAG, "Failed to create LVGL display");
        return false;
    }

    // Register the callback for color transfer completion
    esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    esp_lcd_panel_io_register_event_callbacks(ioHandle, &cbs, lvglDisplay);

    g_display_instance = this;
    LOG_I(TAG, "LVGL display created successfully");
    return true;
}

bool St7789i8080Display::stopLvgl() {
    if (lvglDisplay) {
        lvgl_port_remove_disp(lvglDisplay);
        lvglDisplay = nullptr;
    }
    return true;
}
