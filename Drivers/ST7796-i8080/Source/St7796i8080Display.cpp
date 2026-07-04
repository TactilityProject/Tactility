#include "St7796i8080Display.h"
#include <tactility/log.h>
#include <driver/gpio.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

constexpr auto* TAG = "St7796i8080Display";
static St7796i8080Display* g_display_instance = nullptr;

St7796i8080Display::St7796i8080Display(const Configuration& config) 
    : configuration(config), lock(std::make_shared<std::mutex>()) {
    
    // Validate configuration
    if (!configuration.isValid()) {
        LOG_E(TAG, "Invalid configuration: resolution must be set");
        return;
    }
}

bool St7796i8080Display::createI80Bus() {
    LOG_I(TAG, "Creating I80 bus");
    
    // Create I80 bus configuration
    esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num = configuration.dcPin,
        .wr_gpio_num = configuration.wrPin,
        .clk_src = LCD_CLK_SRC_PLL160M,
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
    
    if (esp_lcd_new_i80_bus(&bus_cfg, &i80BusHandle) != ESP_OK) {
        LOG_E(TAG, "Failed to create I80 bus");
        return false;
    }

    return true;
}

bool St7796i8080Display::createPanelIO() {
    LOG_I(TAG, "Creating panel IO");
    
    // Create panel IO
    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = configuration.csPin,
        .pclk_hz = configuration.pixelClockFrequency,
        .trans_queue_depth = configuration.transactionQueueDepth,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
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

    if (esp_lcd_new_panel_io_i80(i80BusHandle, &io_cfg, &ioHandle) != ESP_OK) {
        LOG_E(TAG, "Failed to create panel");
        return false;
    }

    return true;
}

bool St7796i8080Display::createPanel() {
    LOG_I(TAG, "Configuring panel");
    
    // Create ST7796 panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = configuration.resetPin,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = false
        },
        .vendor_config = nullptr
    };
    
    if (esp_lcd_new_panel_st7796(ioHandle, &panel_config, &panelHandle) != ESP_OK) {
        LOG_E(TAG, "Failed to create panel");
        return false;
    }

    // Reset panel
    if (esp_lcd_panel_reset(panelHandle) != ESP_OK) {
        LOG_E(TAG, "Failed to reset panel");
        return false;
    }

    // Initialize panel
    if (esp_lcd_panel_init(panelHandle) != ESP_OK) {
        LOG_E(TAG, "Failed to init panel");
        return false;
    }

    // Set swap XY
    if (esp_lcd_panel_swap_xy(panelHandle, configuration.swapXY) != ESP_OK) {
        LOG_E(TAG, "Failed to swap XY ");
        return false;
    }

    // Set mirror
    if (esp_lcd_panel_mirror(panelHandle, configuration.mirrorX, configuration.mirrorY) != ESP_OK) {
        LOG_E(TAG, "Failed to set panel to mirror");
        return false;
    }

    // Set inversion
    if (esp_lcd_panel_invert_color(panelHandle, configuration.invertColor) != ESP_OK) {
        LOG_E(TAG, "Failed to set panel to invert");
        return false;
    }

    // Turn on display
    if (esp_lcd_panel_disp_on_off(panelHandle, true) != ESP_OK) {
        LOG_E(TAG, "Failed to turn display on");
        return false;
    }
    
    return true;
}

bool St7796i8080Display::start() {
    LOG_I(TAG, "Initializing I8080 ST7796 Display hardware...");
    
    // Calculate buffer size if needed
    configuration.calculateBufferSize();

    // Allocate buffer
    size_t buffer_size = configuration.bufferSize * LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565);
    displayBuffer = (uint8_t*)heap_caps_malloc(buffer_size, MALLOC_CAP_DMA);
    if (!displayBuffer) {
        LOG_E(TAG, "Failed to allocate display buffer");
        return false;
    }

    // Create I80 bus
    if (!createI80Bus()) {
        stop();
        return false;
    }
    
    // Create panel IO
    if (!createPanelIO()) {
        stop();
        return false;
    }
    
    // Create panel
    if (!createPanel()) {
        stop();
        return false;
    }

    LOG_I(TAG, "Display hardware initialized");
    return true;
}

bool St7796i8080Display::stop() {
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

    // Free buffer 1
    if (displayBuffer) {
        heap_caps_free(displayBuffer);
        displayBuffer = nullptr;
    }

    // Turn off backlight
    if (configuration.backlightDutyFunction) {
        configuration.backlightDutyFunction(0);
    }

    return true;
}

bool St7796i8080Display::startLvgl() {
    LOG_I(TAG, "Initializing LVGL for ST7796 display");

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
            .sw_rotate = false,
            .swap_bytes = false,
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

    g_display_instance = this;
    LOG_I(TAG, "LVGL display created successfully");
    return true;
}

bool St7796i8080Display::stopLvgl() {
    if (lvglDisplay) {
        lvgl_port_remove_disp(lvglDisplay);
        lvglDisplay = nullptr;
    }
    return true;
}