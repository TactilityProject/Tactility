#pragma once

#include <Tactility/hal/display/DisplayDriver.h>

#include <esp_lcd_panel_ops.h>
#if CONFIG_SOC_MIPI_DSI_SUPPORTED
#include <esp_lcd_mipi_dsi.h>
#endif

class EspLcdDisplayDriver : public tt::hal::display::DisplayDriver {

    esp_lcd_panel_handle_t panelHandle;
    uint16_t hRes;
    uint16_t vRes;
    tt::hal::display::ColorFormat colorFormat;

public:

    EspLcdDisplayDriver(
        esp_lcd_panel_handle_t panelHandle,
        uint16_t hRes,
        uint16_t vRes,
        tt::hal::display::ColorFormat colorFormat
    ) : panelHandle(panelHandle), hRes(hRes), vRes(vRes), colorFormat(colorFormat) {}

    tt::hal::display::ColorFormat getColorFormat() const override {
        return colorFormat;
    }

    bool drawBitmap(int xStart, int yStart, int xEnd, int yEnd, const void* pixelData) override {
        bool result = esp_lcd_panel_draw_bitmap(panelHandle, xStart, yStart, xEnd, yEnd, pixelData) == ESP_OK;
        return result;
    }

    uint16_t getPixelWidth() const override { return hRes; }

    uint16_t getPixelHeight() const override { return vRes; }

#if CONFIG_SOC_MIPI_DSI_SUPPORTED
    uint8_t getFrameBuffers(void* outBuffers[2]) const override {
        if (outBuffers == nullptr) {
            return 0;
        }
        return (esp_lcd_dpi_panel_get_frame_buffer(panelHandle, 2, &outBuffers[0], &outBuffers[1]) == ESP_OK) ? 2 : 0;
    }
#endif
};
