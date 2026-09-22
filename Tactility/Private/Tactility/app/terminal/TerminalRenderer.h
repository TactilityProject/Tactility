#pragma once

#include <cstdint>

struct Device;

/**
 * Draws the active vterm's cell grid to a display device.
 *
 * Owns everything about the terminal grid that doesn't depend on how pixels actually reach the
 * panel: glyph painting, the shadow buffer used to skip unchanged cells, and cursor blink state.
 * A subclass supplies the panel/frame dimension relationship and how the off-screen frame buffer
 * gets onto the display: see TerminalRendererPpa (hardware rotate, for a landscape terminal on a
 * portrait panel) and TerminalRendererGeneric (direct blit, panel's native orientation, no extra
 * hardware needed).
 *
 * Because presenting a frame is a whole-buffer operation, redraws are frame-based rather than
 * per-row: changed cells are painted into the off-screen buffer, and the whole buffer is only
 * pushed when something actually changed.
 *
 * Must be used only while LVGL is stopped, since it writes to the display directly.
 */
class TerminalRenderer {
public:
    virtual ~TerminalRenderer() = default;

    /**
     * Allocates buffers.
     *
     * @param display the display device to draw to
     *
     * Glyphs are drawn at their native size, never scaled up or down.
     */
    virtual bool begin(Device* display) = 0;

    /** Releases all resources. */
    virtual void end() = 0;

    /**
     * Repaints changed cells and presents the frame. Pass true to redraw everything.
     *
     * Also drives the cursor blink, so this must be called regularly even when nothing has changed.
     * The blink phase advances on wall-clock time rather than on frame count.
     */
    void render(bool force = false);

    /** Terminal grid dimensions. */
    int columns() const { return cols; }
    int rows() const { return rowCount; }

protected:
    void paintCell(int row, int col, char ch, uint8_t attr);
    void paintCursor(int row, int col);

    /** Pushes frameBuffer (frameWidth x frameHeight) onto the display. */
    virtual void present() = 0;

    /**
     * Common begin() work: computes the cell grid from frameWidth/frameHeight and allocates
     * frameBuffer + the shadow grid. The subclass must set display/panelWidth/panelHeight/
     * frameWidth/frameHeight first.
     */
    bool allocateCommon(Device* display);

    /** Frees frameBuffer, the shadow grid, and the borrowed hw double buffer, if any. */
    void freeCommon();

    /**
     * If the display reports two panel-sized frame buffers, borrows them as hwFrameBuffers[0/1]
     * and sets usingHwFrameBuffer, so the display controller can scan out one while the next frame
     * is written into the other. Both buffers are panelWidth x panelHeight regardless of whether
     * the subclass rotates into them.
     */
    bool acquireHwDoubleBuffer();

    Device* display = nullptr;

    // Off-screen buffer glyphs are painted into, before being pushed to the display.
    uint16_t* frameBuffer = nullptr;
    int frameWidth = 0;
    int frameHeight = 0;

    // Panel dimensions in its own native orientation.
    int panelWidth = 0;
    int panelHeight = 0;

    // Either the panel's own double buffers (see acquireHwDoubleBuffer()), or left unset if a
    // subclass needs its own single output buffer instead.
    uint16_t* hwFrameBuffers[2] = { nullptr, nullptr };
    int backBufferIndex = 1;
    bool usingHwFrameBuffer = false;

    // Copy of what has been painted, used to skip unchanged cells.
    struct Cell {
        char ch;
        uint8_t attr;
    };
    Cell* shadow = nullptr;

    // Cursor blink state. The cell under the cursor is drawn inverted while the blink is on, and
    // its position is remembered so it can be restored to normal when the cursor moves or blinks off.
    int cursorRow = -1;
    int cursorCol = -1;
    bool cursorDrawn = false;
    bool blinkOn = true;
    uint32_t lastBlinkMs = 0;

    int cols = 0;
    int rowCount = 0;

    // Cell size on screen. Glyphs are drawn at native size, so this equals TT_TERMINAL_FONT_SYMBOL's
    // glyph_width/glyph_height (see TerminalRenderer.cpp).
    int cellWidth = 0;
    int cellHeight = 0;

    // Pixel offset of the grid within the frame. The cell size rarely divides the panel exactly,
    // so the remainder is split between opposite edges rather than left as a strip at one end.
    int originX = 0;
    int originY = 0;
};
