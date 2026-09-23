#include <Tactility/app/terminal/TerminalRenderer.h>

#include <Tactility/app/terminal/Scrollback.h>

#include <font/fonts.h>
#include <font/render.h>

// The font struct to use, e.g. ibmplexmono_14_font (see font/fonts.h) - set per device/build via
// a compile definition; that font's Kconfig entry (Modules/font-module/Kconfig) must be enabled.
#ifndef TT_TERMINAL_FONT_SYMBOL
#error TT_TERMINAL_FONT_SYMBOL is not set
#endif

#include <tactility/drivers/display.h>
#include <tactility/log.h>
#include <tactility/memory.h>
#include <tactility/time.h>

#ifdef ESP_PLATFORM
#include <esp_cache.h>
#endif

#include <cstdlib>
#include <cstring>

extern "C" {
#include <Tactility/app/terminal/vterm/vterm.h>
}

constexpr auto* TAG = "TermRender";

namespace {

// Half-period of the cursor blink: on for this long, then off for this long.
constexpr uint32_t CURSOR_BLINK_INTERVAL_MS = 500;

/*
 * The 16 ANSI colours come from vterm rather than being duplicated here, so vterm's palette stays
 * the single source of truth: a program that recolours the terminal (plasma sets a 16-entry RGB565
 * ramp) actually affects what appears.
 *
 * Index is vterm's 4-bit colour: 0-7 normal, 8-15 bright.
 */
inline uint16_t paletteColour(uint8_t index) {
    return vterm_get_palette()[index & 0x0F];
}

} // namespace

bool TerminalRenderer::allocateCommon(Device* displayDevice) {
    display = displayDevice;

    cellWidth = TT_TERMINAL_FONT_SYMBOL.glyph_width;
    cellHeight = TT_TERMINAL_FONT_SYMBOL.glyph_height;

    cols = frameWidth / cellWidth;
    rowCount = frameHeight / cellHeight;
    if (cols > VTERM_COLS) {
        cols = VTERM_COLS;
    }
    if (rowCount > VTERM_ROWS) {
        rowCount = VTERM_ROWS;
    }
    if (cols <= 0 || rowCount <= 0) {
        LOG_E(TAG, "Panel %dx%d is too small for a text grid", panelWidth, panelHeight);
        return false;
    }

    /*
     * The grid rarely divides the panel exactly (720 pixels of height at 32 pixels a cell leaves
     * half a cell over), so the slack is split between the two edges rather than all left at the
     * bottom, where it shows as a strip of dead space below the last row.
     */
    originX = (frameWidth - cols * cellWidth) / 2;
    originY = (frameHeight - rowCount * cellHeight) / 2;

    const size_t frameBytes = static_cast<size_t>(frameWidth) * frameHeight * sizeof(uint16_t);

    // 64-byte aligned for DMA; PSRAM is fine at this size and internal RAM is scarce.
    const MemoryPolicy frameBufferPolicy { .required = MEMORY_CAPABILITY_EXTERNAL, .desired = 0, .alignment = 64 };
    frameBuffer = static_cast<uint16_t*>(memory_alloc_with_policy(frameBytes, &frameBufferPolicy));
    if (frameBuffer == nullptr) {
        LOG_E(TAG, "Failed to allocate %u byte frame buffer", (unsigned)frameBytes);
        return false;
    }
    memset(frameBuffer, 0, frameBytes);

    shadow = static_cast<Cell*>(calloc(static_cast<size_t>(cols) * rowCount, sizeof(Cell)));
    if (shadow == nullptr) {
        LOG_E(TAG, "Failed to allocate shadow grid");
        memory_free(frameBuffer);
        frameBuffer = nullptr;
        return false;
    }

    return true;
}

void TerminalRenderer::freeCommon() {
    if (frameBuffer != nullptr) {
        memory_free(frameBuffer);
        frameBuffer = nullptr;
    }
    free(shadow);
    shadow = nullptr;
    usingHwFrameBuffer = false;
    hwFrameBuffers[0] = nullptr;
    hwFrameBuffers[1] = nullptr;
    display = nullptr;
}

bool TerminalRenderer::acquireHwDoubleBuffer() {
    if (display_get_frame_buffer_count(display) != 2) {
        return false;
    }
    void* fb0 = nullptr;
    void* fb1 = nullptr;
    display_get_frame_buffer(display, 0, &fb0);
    display_get_frame_buffer(display, 1, &fb1);
    if (fb0 == nullptr || fb1 == nullptr) {
        return false;
    }

    hwFrameBuffers[0] = static_cast<uint16_t*>(fb0);
    hwFrameBuffers[1] = static_cast<uint16_t*>(fb1);
    usingHwFrameBuffer = true;
    backBufferIndex = 1;

    const size_t panelBytes = static_cast<size_t>(panelWidth) * panelHeight * sizeof(uint16_t);
    memset(hwFrameBuffers[0], 0, panelBytes);
    memset(hwFrameBuffers[1], 0, panelBytes);
#ifdef ESP_PLATFORM
    esp_cache_msync(hwFrameBuffers[0], panelBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    esp_cache_msync(hwFrameBuffers[1], panelBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
#endif
    LOG_I(TAG, "Using hardware double buffering");
    return true;
}

void TerminalRenderer::paintCell(int row, int col, char ch, uint8_t attr) {
    const uint16_t fg = paletteColour(VTERM_ATTR_FG(attr));
    const uint16_t bg = paletteColour(VTERM_ATTR_BG(attr));

    uint16_t* out = &frameBuffer[(originY + row * cellHeight) * frameWidth + originX + col * cellWidth];
    font_render_char(&TT_TERMINAL_FONT_SYMBOL, ch, fg, bg, out, frameWidth);
}

void TerminalRenderer::paintCursor(int row, int col) {
    if (shadow == nullptr) {
        return;
    }

    // The cell underneath comes from the shadow rather than from vterm's grid directly, since it
    // reflects what render() actually drew this frame (e.g. a row served from scrollback).
    const Cell& cell = shadow[row * cols + col];

    // Foreground and background swapped, which reads as a block cursor over whatever is there.
    const uint8_t inverted = VTERM_ATTR(VTERM_ATTR_BG(cell.attr), VTERM_ATTR_FG(cell.attr));
    paintCell(row, col, cell.ch, inverted);
}

void TerminalRenderer::render(bool force) {
    const vterm_cell_t* cells = vterm_get_direct_buffer();
    if (cells == nullptr || frameBuffer == nullptr || shadow == nullptr) {
        return;
    }

    bool changed = force;

    // Scratch row used when a line comes from scrollback rather than from the live terminal.
    vterm_cell_t historyRow[VTERM_COLS];

    /*
     * Undo the cursor before comparing against the shadow.
     *
     * The cursor is drawn with inverted attributes that were never in the shadow, so leaving it
     * there would make its cell compare equal and strand an inverted block on screen.
     */
    if (cursorDrawn && cursorRow >= 0 && cursorRow < rowCount && cursorCol >= 0 && cursorCol < cols) {
        char ch = ' ';
        uint8_t attr = VTERM_DEFAULT_ATTR;

        if (cells != nullptr) {
            const vterm_cell_t& under = cells[cursorRow * VTERM_COLS + cursorCol];
            ch = under.ch;
            attr = under.attr;
        }

        paintCell(cursorRow, cursorCol, ch, attr);
        cursorDrawn = false;
        changed = true;
    }

    for (int row = 0; row < rowCount; row++) {
        // While the view is scrolled back, the upper rows are served from history and the rest
        // still come from the live screen, so the two meet seamlessly mid-scroll.
        const bool fromHistory = Scrollback::rowForDisplay(row, rowCount, historyRow);
        const vterm_cell_t* source = fromHistory ? historyRow : &cells[row * VTERM_COLS];

        for (int col = 0; col < cols; col++) {
            const char ch = source[col].ch;
            const uint8_t attr = source[col].attr;
            Cell& previous = shadow[row * cols + col];

            if (!force && ch == previous.ch && attr == previous.attr) {
                continue;
            }

            paintCell(row, col, ch, attr);
            previous = Cell { ch, attr };
            changed = true;
        }
    }

    // Advance the blink on wall-clock time, so the rate doesn't depend on how often render() runs.
    const uint32_t now = get_micros_since_boot() / 1000;
    if (now - lastBlinkMs >= CURSOR_BLINK_INTERVAL_MS) {
        lastBlinkMs = now;
        blinkOn = !blinkOn;
    }

    int col = 0;
    int row = 0;
    int visible = 0;
    vterm_get_cursor(vterm_get_active(), &col, &row, &visible);

    // Typing should not make the cursor flicker off mid-keystroke, so restart the blink cycle
    // whenever it moves: the cursor is then solid for a full interval at its new position.
    if (row != cursorRow || col != cursorCol) {
        cursorRow = row;
        cursorCol = col;
        blinkOn = true;
        lastBlinkMs = now;
    }

    // No cursor while looking at history: it marks where typing goes, which is on the live screen.
    if (Scrollback::offset() > 0) {
        visible = 0;
    }

    if (visible && blinkOn && row >= 0 && row < rowCount && col >= 0 && col < cols) {
        paintCursor(row, col);
        cursorDrawn = true;
        changed = true;
    }

    // Presenting is a whole-frame operation, so it only runs when something actually changed; an
    // idle prompt costs only the comparison loop and the blink toggle.
    if (changed) {
        present();
    }
}
