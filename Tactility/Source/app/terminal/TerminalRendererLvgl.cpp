#include <Tactility/app/terminal/TerminalRendererLvgl.h>

#include <graphics/pixel_buffer.h>

#include <lvgl/lvgl.h>

#include <tactility/log.h>

constexpr auto* TAG = "TermRenderLvgl";

TerminalRendererLvgl::~TerminalRendererLvgl() {
    end();
}

bool TerminalRendererLvgl::begin(Device* displayDevice) {
    lvgl_lock();
    lv_obj_t* canvas = canvas_;
    if (canvas == nullptr) {
        lvgl_unlock();
        LOG_E(TAG, "No canvas attached");
        return false;
    }
    lv_obj_update_layout(canvas);
    frameWidth = lv_obj_get_width(canvas);
    frameHeight = lv_obj_get_height(canvas);
    lvgl_unlock();

    panelWidth = frameWidth;
    panelHeight = frameHeight;

    // LVGL converts to the display's own format when flushing.
    if (!allocateCommon(displayDevice, DISPLAY_COLOR_FORMAT_RGB565)) {
        return false;
    }

    canvasBuffer_ = pixel_buffer_create(DISPLAY_COLOR_FORMAT_RGB565, frameWidth, frameHeight);
    if (canvasBuffer_ == nullptr) {
        LOG_E(TAG, "Failed to allocate canvas buffer");
        freeCommon();
        return false;
    }

    clearPanelOnce();

    lvgl_lock();
    attachCanvas(canvas_);
    lvgl_unlock();

    LOG_I(TAG, "Terminal %dx%d cells (%dx%d px) on %dx%d canvas",
             cols, rowCount, cellWidth, cellHeight, frameWidth, frameHeight);
    return true;
}

void TerminalRendererLvgl::end() {
    if (canvasBuffer_ != nullptr) {
        lvgl_lock();
        lv_obj_t* canvas = canvas_;
        if (canvas != nullptr) {
            lv_obj_delete(canvas);
            canvas_ = nullptr;
        }
        lvgl_unlock();

        pixel_buffer_free(canvasBuffer_);
        canvasBuffer_ = nullptr;
    }
    freeCommon();
}

void TerminalRendererLvgl::attachCanvas(lv_obj_t* canvas) {
    canvas_ = canvas;
    if (canvas != nullptr && canvasBuffer_ != nullptr) {
        lv_canvas_set_buffer(canvas, pixel_buffer_get_data(canvasBuffer_), frameWidth, frameHeight, LV_COLOR_FORMAT_RGB565);
    }
}

void TerminalRendererLvgl::present(int yStart, int yEnd) {
    // Locked during the blit too, as LVGL may be drawing the canvas from this buffer
    lvgl_lock();
    pixel_buffer_blit(canvasBuffer_, 0, yStart, frameBuffer, 0, 0, frameWidth, yEnd - yStart, PIXEL_BUFFER_CONVERSION_EXACT_BLACK);

    lv_obj_t* canvas = canvas_;
    if (canvas != nullptr) {
        lv_area_t area;
        lv_obj_get_coords(canvas, &area);
        area.y2 = area.y1 + yEnd - 1;
        area.y1 += yStart;
        lv_obj_invalidate_area(canvas, &area);
    }
    lvgl_unlock();
}
