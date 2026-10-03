#pragma once

#include <Tactility/app/terminal/TerminalRenderer.h>

#include <lvgl.h>

#include <atomic>

/**
 * Draws the terminal into an LVGL canvas, so it can live in a regular window while LVGL keeps
 * running. The frame is kept in a canvas-sized buffer that survives the canvas itself being deleted
 * and recreated by the window manager.
 */
class TerminalRendererLvgl : public TerminalRenderer {
public:
    ~TerminalRendererLvgl() override;

    /** Sizes the grid to the canvas given to attachCanvas(), which must happen first. */
    bool begin(Device* display) override;

    /** Releases all resources and deletes the attached canvas, since it would still show the freed buffer. */
    void end() override;

    bool isShown() const override { return canvas_ != nullptr; }

    /**
     * Shows the frame on the given canvas, or detaches from it when nullptr.
     * @warning Caller must hold the LVGL lock.
     */
    void attachCanvas(lv_obj_t* canvas);

protected:
    void present(int yStart, int yEnd) override;

private:
    // Owned by the LVGL lock, but read without it by isShown().
    std::atomic<lv_obj_t*> canvas_ = nullptr;

    // The whole frame, as shown by the canvas.
    PixelBuffer* canvasBuffer_ = nullptr;
};
