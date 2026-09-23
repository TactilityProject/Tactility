#pragma once

#include <cstddef>
#include <cstdint>

struct Device;
struct PixelBuffer;

/**
 * Draws the active vterm's cell grid to a display device.
 *
 * Glyph painting, the shadow buffer, cursor blink, and per-row dirty-tracking live here. A
 * subclass supplies the panel/frame relationship and how a row reaches the display: see
 * TerminalRendererPpa (hardware rotate) and TerminalRendererGeneric (direct blit).
 *
 * Painted and presented one row at a time, so frameBuffer only ever holds a single row.
 * presentRegion()/finishFrame() accumulate rows and flip/push the whole buffer once per frame - a
 * per-row push would show a mix of stale and fresh rows, since a hw double buffer's zero-copy
 * path flips on any write regardless of the sub-rect given.
 *
 * Must be used only while LVGL is stopped, since it writes to the display directly.
 */
class TerminalRenderer {
public:
    virtual ~TerminalRenderer() = default;

    /** Allocates buffers. Glyphs are drawn at native size, never scaled. */
    virtual bool begin(Device* display) = 0;

    /** Releases all resources. */
    virtual void end() = 0;

    /**
     * Repaints changed cells and presents the frame. Pass true to redraw everything.
     * Also drives the cursor blink, so must be called regularly even when nothing has changed.
     */
    void render(bool force = false);

    /** Terminal grid dimensions. */
    int columns() const { return cols; }
    int rows() const { return rowCount; }

protected:
    void paintCell(int row, int col, char ch, uint8_t attr);
    void paintCursor(int row, int col);

    /**
     * Turns frameBuffer's current row (pixels [yStart, yEnd)) into panel-oriented pixels and
     * hands them to presentRegion() - or writes directly into the hw double buffer and sets
     * hwBufferDirty itself, skipping presentRegion().
     */
    virtual void present(int yStart, int yEnd) = 0;

    /**
     * Common begin() work: computes the cell grid and allocates frameBuffer (one row) + shadow.
     * The subclass must set display/panelWidth/panelHeight/frameWidth/frameHeight first.
     */
    bool allocateCommon(Device* display);

    /** Frees frameBuffer, fullFrameBuffer, shadow, and the borrowed hw double buffer, if any. */
    void freeCommon();

    /**
     * Zeroes frameBuffer and presents it across the full panel height once, blanking the
     * letterbox margins. Call from begin(), after allocateCommon() and any subclass output buffer
     * are ready.
     */
    void clearPanelOnce();

    /**
     * Queries DISPLAY_CAPABILITY_REQUIRES_FULL_FRAME and DISPLAY_COLOR_FORMAT_MONOCHROME
     * (monochrome always implies full-frame here, regardless of the driver's own flag). If either
     * applies and there's no hw double buffer, allocates a panel-sized buffer for
     * presentRegion()/finishFrame() to accumulate into, since such a display can't take a partial
     * display_draw_bitmap() rect. Call from begin(), after acquireHwDoubleBuffer() and any
     * subclass output buffer are ready.
     * @retval false allocation failed; the subclass's begin() should fail too
     */
    bool allocateFullFrameBufferIfNeeded();

    /**
     * Places src's full regionW x regionH extent at (regionX, regionY) via pixel_buffer_blit().
     * Accumulates into hwFrameBuffers or fullFrameBuffer (see finishFrame()) when either applies,
     * otherwise pushes directly via display_draw_bitmap() as a genuine partial update.
     */
    void presentRegion(struct PixelBuffer* src, int regionX, int regionY, int regionW, int regionH);

    /**
     * Seeds hwFrameBuffers[backBufferIndex] from the other buffer on the first write since the
     * last flip, since only rows touched this frame get repainted afterward. A subclass writing
     * directly into hwFrameBuffers (bypassing presentRegion()) must call this itself first.
     */
    void ensureBackBufferSeeded();

    /**
     * Call once after render()'s row loop. Pushes whichever buffer accumulated writes this
     * frame, whole, and flips backBufferIndex in the hw case.
     */
    void finishFrame();

    /** Wraps the display's two panel-sized frame buffers as hwFrameBuffers[0/1] (non-owning - see
     * pixel_buffer_wrap()) and sets usingHwFrameBuffer, if it reports any. */
    bool acquireHwDoubleBuffer();

    Device* display = nullptr;

    // True for a monochrome display. frameBuffer/hwFrameBuffers/fullFrameBuffer format-handling
    // all lives in graphics-module now (see paintCell()/presentRegion()).
    bool monochrome = false;

    // Scratch buffer glyphs are painted into before being pushed. Sized for one text row, not the
    // whole frame.
    PixelBuffer* frameBuffer = nullptr;
    int frameWidth = 0;
    int frameHeight = 0;

    // Pixel y of the row currently being painted; paintCell() subtracts it to index into the
    // small, reused frameBuffer.
    int currentRowYOffset = 0;

    // Panel dimensions in its own native orientation.
    int panelWidth = 0;
    int panelHeight = 0;

    // The panel's own double buffers (acquireHwDoubleBuffer()), wrapped non-owning, or unset if a
    // subclass uses its own output buffer instead.
    PixelBuffer* hwFrameBuffers[2] = { nullptr, nullptr };
    int backBufferIndex = 1;
    bool usingHwFrameBuffer = false;
    // Set when hwFrameBuffers[backBufferIndex] has unflushed writes; finishFrame() flips only then.
    bool hwBufferDirty = false;

    // See allocateFullFrameBufferIfNeeded()/presentRegion()/finishFrame().
    bool fullFrameRequired = false;
    PixelBuffer* fullFrameBuffer = nullptr;
    bool fullFrameDirty = false;

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
