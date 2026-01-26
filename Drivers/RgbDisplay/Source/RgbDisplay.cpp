#include "RgbDisplay.h"

#include <Tactility/Check.h>
#include <Tactility/hal/touch/TouchDevice.h>
#include <Tactility/Logger.h>

#include <esp_err.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lvgl_port.h>

static const auto LOGGER = tt::Logger("RgbDisplay");

RgbDisplay::~RgbDisplay() {
    check(
        displayDriver == nullptr || displayDriver.use_count() == 0,
        "DisplayDriver is still in use. This will cause memory access violations."
    );
}

bool RgbDisplay::start() {
    LOGGER.info("Starting");

    if (esp_lcd_new_rgb_panel(&configuration->panelConfig, &panelHandle) != ESP_OK) {
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

    if (esp_lcd_panel_swap_xy(panelHandle, configuration->swapXY) != ESP_OK) {
        LOGGER.error("Failed to swap XY");
        return false;
    }

    if (esp_lcd_panel_mirror(panelHandle, configuration->mirrorX, configuration->mirrorY) != ESP_OK) {
        LOGGER.error("Failed to set panel to mirror");
        return false;
    }

    if (esp_lcd_panel_invert_color(panelHandle, configuration->invertColor) != ESP_OK) {
        LOGGER.error("Failed to set panel to invert");
        return false;
    }

    return true;
}

bool RgbDisplay::stop() {
    if (lvglDisplay != nullptr) {
        stopLvgl();
        lvglDisplay = nullptr;
    }

    if (panelHandle != nullptr && esp_lcd_panel_del(panelHandle) != ESP_OK) {
        return false;
    }

    if (displayDriver != nullptr && displayDriver.use_count() > 1) {
        LOGGER.warn("DisplayDriver is still in use.");
    }

    auto touch_device = getTouchDevice();
    if (touch_device != nullptr) {
        touch_device->startLvgl(lvglDisplay);
    }

    return true;
}


bool RgbDisplay::startLvgl() {
    assert(lvglDisplay == nullptr);

    if (displayDriver != nullptr && displayDriver.use_count() > 1) {
        LOGGER.warn("DisplayDriver is still in use.");
    }

    auto display_config = getLvglPortDisplayConfig();

    const lvgl_port_display_rgb_cfg_t rgb_config = {
        .flags = {
            .bb_mode = configuration->bufferConfiguration.bounceBufferMode,
            .avoid_tearing = configuration->bufferConfiguration.avoidTearing
        }
    };

    lvglDisplay = lvgl_port_add_disp_rgb(&display_config, &rgb_config);
    LOGGER.info("Finished");

    auto touch_device = getTouchDevice();
    if (touch_device != nullptr) {
        touch_device->startLvgl(lvglDisplay);
    }

    return lvglDisplay != nullptr;
}

bool RgbDisplay::stopLvgl() {
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

lvgl_port_display_cfg_t RgbDisplay::getLvglPortDisplayConfig() const {
    return {
        .io_handle = nullptr,
        .panel_handle = panelHandle,
        .control_handle = nullptr,
        .buffer_size = configuration->bufferConfiguration.size,
        .double_buffer = configuration->bufferConfiguration.doubleBuffer,
        .trans_size = 0,
        .hres = configuration->panelConfig.timings.h_res,
        .vres = configuration->panelConfig.timings.v_res,
        .monochrome = false,
        .rotation = {
            .swap_xy = configuration->swapXY,
            .mirror_x = configuration->mirrorX,
            .mirror_y = configuration->mirrorY,
        },
        .color_format = configuration->colorFormat,
        .flags = {
            .buff_dma = !configuration->bufferConfiguration.useSpi,
            .buff_spiram = configuration->bufferConfiguration.useSpi,
            .sw_rotate = false,
            .swap_bytes = false,
            .full_refresh = false,
            .direct_mode = false
        }
    };
}

