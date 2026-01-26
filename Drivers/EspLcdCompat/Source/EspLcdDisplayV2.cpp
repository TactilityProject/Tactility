#include "EspLcdDisplayV2.h"
#include "EspLcdDisplayDriver.h"

#include <cassert>
#include <esp_lvgl_port_disp.h>
#include <Tactility/Check.h>
#include <Tactility/Logger.h>
#include <Tactility/hal/touch/TouchDevice.h>

static const auto LOGGER = tt::Logger("EspLcdDispV2");

inline unsigned int getBufferSize(const std::shared_ptr<EspLcdConfiguration>& configuration) {
    if (configuration->bufferSize != DEFAULT_BUFFER_SIZE) {
        return configuration->bufferSize;
    } else {
        return configuration->horizontalResolution * (configuration->verticalResolution / 10);
    }
}

EspLcdDisplayV2::~EspLcdDisplayV2() {
    check(
        displayDriver == nullptr || displayDriver.use_count() < 2, // 1 reference is held by this class
        "DisplayDriver is still in use. This will cause memory access violations."
    );
}

bool EspLcdDisplayV2::applyConfiguration() const {
    if (esp_lcd_panel_reset(panelHandle) != ESP_OK) {
        LOGGER.error("Failed to reset panel");
        return false;
    }

    if (esp_lcd_panel_init(panelHandle) != ESP_OK) {
        LOGGER.error("Failed to init panel");
        return false;
    }

    if (configuration->invertColor && esp_lcd_panel_invert_color(panelHandle, configuration->invertColor) != ESP_OK) {
        LOGGER.error("Failed to set panel to invert");
        return false;
    }

    // Warning: it looks like LVGL rotation is broken when "gap" is set and the screen is moved to a non-default orientation
    int gap_x = configuration->swapXY ? configuration->gapY : configuration->gapX;
    int gap_y = configuration->swapXY ? configuration->gapX : configuration->gapY;
    bool should_set_gap = gap_x != 0 || gap_y != 0;
    if (should_set_gap && esp_lcd_panel_set_gap(panelHandle, gap_x, gap_y) != ESP_OK) {
        LOGGER.error("Failed to set panel gap");
        return false;
    }

    if (configuration->swapXY && esp_lcd_panel_swap_xy(panelHandle, configuration->swapXY) != ESP_OK) {
        LOGGER.error("Failed to swap XY ");
        return false;
    }

    bool should_set_mirror = configuration->mirrorX || configuration->mirrorY;
    if (should_set_mirror && esp_lcd_panel_mirror(panelHandle, configuration->mirrorX, configuration->mirrorY) != ESP_OK) {
        LOGGER.error("Failed to set panel to mirror");
        return false;
    }

    if (configuration->invertColor && esp_lcd_panel_invert_color(panelHandle, configuration->invertColor) != ESP_OK) {
        LOGGER.error("Failed to set panel to invert");
        return false;
    }

    if (esp_lcd_panel_disp_on_off(panelHandle, true) != ESP_OK) {
        LOGGER.error("Failed to turn display on");
        return false;
    }

    return true;
}

bool EspLcdDisplayV2::start() {
    if (!createIoHandle(ioHandle)) {
        LOGGER.error("Failed to create IO handle");
        return false;
    }

    esp_lcd_panel_dev_config_t panel_config = createPanelConfig(configuration, configuration->resetPin);

    if (!createPanelHandle(ioHandle, panel_config, panelHandle)) {
        LOGGER.error("Failed to create panel handle");
        esp_lcd_panel_io_del(ioHandle);
        ioHandle = nullptr;
        return false;
    }

    if (!applyConfiguration()) {
        esp_lcd_panel_del(panelHandle);
        panelHandle = nullptr;
        esp_lcd_panel_io_del(ioHandle);
        ioHandle = nullptr;
        return false;
    }

    return true;
}

bool EspLcdDisplayV2::stop() {
    if (lvglDisplay != nullptr) {
        stopLvgl();
        lvglDisplay = nullptr;
    }

    if (panelHandle != nullptr && esp_lcd_panel_del(panelHandle) != ESP_OK) {
        return false;
    }

    if (ioHandle != nullptr && esp_lcd_panel_io_del(ioHandle) != ESP_OK) {
        return false;
    }

    if (displayDriver != nullptr && displayDriver.use_count() > 1) {
        LOGGER.warn("DisplayDriver is still in use.");
    }

    return true;
}

bool EspLcdDisplayV2::startLvgl() {
    assert(lvglDisplay == nullptr);

    if (displayDriver != nullptr && displayDriver.use_count() > 1) {
        LOGGER.warn("DisplayDriver is still in use.");
    }

    auto lvgl_port_config  = getLvglPortDisplayConfig(configuration, ioHandle, panelHandle);

    if (useDsiPanel()) {
        auto dsi_config = getLvglPortDisplayDsiConfig(ioHandle, panelHandle);
        lvglDisplay = lvgl_port_add_disp_dsi(&lvgl_port_config, &dsi_config);
    } else if (isRgbPanel()) {
        auto rgb_config = getLvglPortDisplayRgbConfig(ioHandle, panelHandle);
        lvglDisplay = lvgl_port_add_disp_rgb(&lvgl_port_config, &rgb_config);
    } else {
        lvglDisplay = lvgl_port_add_disp(&lvgl_port_config);
    }

    auto touch_device = getTouchDevice();
    if (touch_device != nullptr && touch_device->supportsLvgl()) {
        touch_device->startLvgl(lvglDisplay);
    }

    return lvglDisplay != nullptr;
}

bool EspLcdDisplayV2::stopLvgl() {
    if (lvglDisplay == nullptr) {
        return false;
    }

    auto touch_device = getTouchDevice();
    if (touch_device != nullptr) {
        touch_device->stopLvgl();
    }

    lvgl_port_remove_disp(lvglDisplay);
    lvglDisplay = nullptr;
    return true;
}

lvgl_port_display_cfg_t EspLcdDisplayV2::getLvglPortDisplayConfig(std::shared_ptr<EspLcdConfiguration> configuration, esp_lcd_panel_io_handle_t ioHandle, esp_lcd_panel_handle_t panelHandle) {
    return lvgl_port_display_cfg_t {
        .io_handle = ioHandle,
        .panel_handle = panelHandle,
        .control_handle = nullptr,
        .buffer_size = getBufferSize(configuration),
        .double_buffer = false,
        .trans_size = 0,
        .hres = configuration->horizontalResolution,
        .vres = configuration->verticalResolution,
        .monochrome = configuration->monochrome,
        .rotation = {
            .swap_xy = configuration->swapXY,
            .mirror_x = configuration->mirrorX,
            .mirror_y = configuration->mirrorY,
        },
        .color_format = configuration->lvglColorFormat,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .swap_bytes = configuration->lvglSwapBytes,
            .full_refresh = 0,
            .direct_mode = 0
        }
    };
}

std::shared_ptr<tt::hal::display::DisplayDriver> EspLcdDisplayV2::getDisplayDriver() {
    assert(lvglDisplay == nullptr); // Still attached to LVGL context. Call stopLvgl() first.
    if (displayDriver == nullptr) {
        auto lvgl_port_config  = getLvglPortDisplayConfig(configuration, ioHandle, panelHandle);
        auto panel_config = createPanelConfig(configuration, GPIO_NUM_NC);

        tt::hal::display::ColorFormat color_format;
        if (lvgl_port_config.color_format == LV_COLOR_FORMAT_I1) {
            color_format = tt::hal::display::ColorFormat::Monochrome;
        } else if (lvgl_port_config.color_format == LV_COLOR_FORMAT_RGB565) {
            if (panel_config.rgb_ele_order == LCD_RGB_ELEMENT_ORDER_RGB) {
                if (lvgl_port_config.flags.swap_bytes) {
                    color_format = tt::hal::display::ColorFormat::RGB565Swapped;
                } else {
                    color_format = tt::hal::display::ColorFormat::RGB565;
                }
            } else {
                if (lvgl_port_config.flags.swap_bytes) {
                    color_format = tt::hal::display::ColorFormat::BGR565Swapped;
                } else {
                    color_format = tt::hal::display::ColorFormat::BGR565;
                }
            }
        } else if (lvgl_port_config.color_format == LV_COLOR_FORMAT_RGB888) {
            color_format = tt::hal::display::ColorFormat::RGB888;
        } else {
            check(false, "unsupported driver");
        }

        displayDriver = std::make_shared<EspLcdDisplayDriver>(
            panelHandle,
            lock,
            lvgl_port_config.hres,
            lvgl_port_config.vres,
            color_format
        );
    }
    return displayDriver;
}

