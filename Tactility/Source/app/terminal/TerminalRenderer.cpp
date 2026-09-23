#include <Tactility/app/terminal/TerminalRenderer.h>

#include <Tactility/app/terminal/Scrollback.h>

#include <font/fonts.h>
#include <font/render.h>

#include <graphics/pixel_buffer.h>

// The font struct to use, e.g. ibmplexmono_14_font (see font/fonts.h) - set per device/build via
// a compile definition; that font's Kconfig entry (Modules/graphics-module/Kconfig) must be enabled.
#ifndef TT_TERMINAL_FONT_SYMBOL
#error TT_TERMINAL_FONT_SYMBOL is not set
#endif

#include <tactility/drivers/display.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <algorithm>
#include <cstdlib>

extern "C" {
#include <Tactility/app/terminal/vterm/vterm.h>
}

constexpr auto* TAG = "TermRender";

namespace {

// Half-period of the cursor blink: on for this long, then off for this long.
constexpr uint32_t CURSOR_BLINK_INTERVAL_MS = 500;

// Palette comes from vterm rather than being duplicated here, so a program that recolours the
// terminal (e.g. plasma) actually affects what appears. Index is vterm's 4-bit colour: 0-7
// normal, 8-15 bright.
inline uint16_t paletteColour(uint8_t index) {
    return vterm_get_palette()[index & 0x0F];
}

} // namespace

bool TerminalRenderer::allocateCommon(Device* displayDevice) {
    display = displayDevice;
    monochrome = display_get_color_format(display) == DISPLAY_COLOR_FORMAT_MONOCHROME;

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

    // The grid rarely divides the panel exactly, so the leftover is split between both edges
    // rather than left as dead space on one side.
    originX = (frameWidth - cols * cellWidth) / 2;
    originY = (frameHeight - rowCount * cellHeight) / 2;

    const enum DisplayColorFormat colorFormat = display_get_color_format(display);
    frameBuffer = pixel_buffer_create(colorFormat, frameWidth, cellHeight);
    if (frameBuffer == nullptr) {
        LOG_E(TAG, "Failed to allocate frame buffer");
        return false;
    }

    shadow = static_cast<Cell*>(calloc(static_cast<size_t>(cols) * rowCount, sizeof(Cell)));
    if (shadow == nullptr) {
        LOG_E(TAG, "Failed to allocate shadow grid");
        pixel_buffer_free(frameBuffer);
        frameBuffer = nullptr;
        return false;
    }

    return true;
}

void TerminalRenderer::freeCommon() {
    if (frameBuffer != nullptr) {
        pixel_buffer_free(frameBuffer);
        frameBuffer = nullptr;
    }
    if (fullFrameBuffer != nullptr) {
        pixel_buffer_free(fullFrameBuffer);
        fullFrameBuffer = nullptr;
    }
    fullFrameRequired = false;
    fullFrameDirty = false;
    free(shadow);
    shadow = nullptr;
    usingHwFrameBuffer = false;
    pixel_buffer_free(hwFrameBuffers[0]);
    pixel_buffer_free(hwFrameBuffers[1]);
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

    const enum DisplayColorFormat colorFormat = display_get_color_format(display);
    hwFrameBuffers[0] = pixel_buffer_wrap(fb0, colorFormat, panelWidth, panelHeight, 0);
    hwFrameBuffers[1] = pixel_buffer_wrap(fb1, colorFormat, panelWidth, panelHeight, 0);
    if (hwFrameBuffers[0] == nullptr || hwFrameBuffers[1] == nullptr) {
        pixel_buffer_free(hwFrameBuffers[0]);
        pixel_buffer_free(hwFrameBuffers[1]);
        hwFrameBuffers[0] = nullptr;
        hwFrameBuffers[1] = nullptr;
        return false;
    }
    usingHwFrameBuffer = true;
    backBufferIndex = 1;

    pixel_buffer_clear(hwFrameBuffers[0]);
    pixel_buffer_clear(hwFrameBuffers[1]);
    pixel_buffer_msync(hwFrameBuffers[0], 0, 0, panelWidth, panelHeight);
    pixel_buffer_msync(hwFrameBuffers[1], 0, 0, panelWidth, panelHeight);
    LOG_I(TAG, "Using hardware double buffering");
    return true;
}

void TerminalRenderer::clearPanelOnce() {
    if (frameBuffer == nullptr) {
        return;
    }

    pixel_buffer_clear(frameBuffer);

    for (int y = 0; y < frameHeight; y += cellHeight) {
        currentRowYOffset = y;
        present(y, std::min(frameHeight, y + cellHeight));
    }
    currentRowYOffset = 0;

    finishFrame();
}

bool TerminalRenderer::allocateFullFrameBufferIfNeeded() {
    fullFrameRequired = monochrome || display_has_capability(display, DISPLAY_CAPABILITY_REQUIRES_FULL_FRAME);
    if (!fullFrameRequired || usingHwFrameBuffer) {
        return true;
    }

    fullFrameBuffer = pixel_buffer_create(display_get_color_format(display), panelWidth, panelHeight);
    if (fullFrameBuffer == nullptr) {
        LOG_E(TAG, "Failed to allocate full-frame buffer");
        return false;
    }
    return true;
}

void TerminalRenderer::ensureBackBufferSeeded() {
    if (!usingHwFrameBuffer || hwBufferDirty) {
        return;
    }
    pixel_buffer_blit(hwFrameBuffers[backBufferIndex], 0, 0, hwFrameBuffers[1 - backBufferIndex], 0, 0, panelWidth, panelHeight, PIXEL_BUFFER_CONVERSION_EXACT_BLACK);
    pixel_buffer_msync(hwFrameBuffers[backBufferIndex], 0, 0, panelWidth, panelHeight);
}

void TerminalRenderer::presentRegion(PixelBuffer* src, int regionX, int regionY, int regionW, int regionH) {
    if (usingHwFrameBuffer) {
        ensureBackBufferSeeded();
        pixel_buffer_blit(hwFrameBuffers[backBufferIndex], regionX, regionY, src, 0, 0, regionW, regionH, PIXEL_BUFFER_CONVERSION_EXACT_BLACK);
        pixel_buffer_msync(hwFrameBuffers[backBufferIndex], regionX, regionY, regionW, regionH);
        hwBufferDirty = true;
    } else if (fullFrameRequired) {
        // No seeding needed here, unlike the hw double buffer: there's only one such buffer, so
        // it stays correct incrementally across calls.
        pixel_buffer_blit(fullFrameBuffer, regionX, regionY, src, 0, 0, regionW, regionH, PIXEL_BUFFER_CONVERSION_EXACT_BLACK);
        fullFrameDirty = true;
    } else {
        display_draw_bitmap(display, regionX, regionY, regionX + regionW, regionY + regionH, pixel_buffer_get_data(src));
    }
}

void TerminalRenderer::finishFrame() {
    if (usingHwFrameBuffer && hwBufferDirty) {
        display_draw_bitmap(display, 0, 0, panelWidth, panelHeight, pixel_buffer_get_data(hwFrameBuffers[backBufferIndex]));
        backBufferIndex = 1 - backBufferIndex;
        hwBufferDirty = false;
    } else if (fullFrameRequired && fullFrameDirty) {
        display_draw_bitmap(display, 0, 0, panelWidth, panelHeight, pixel_buffer_get_data(fullFrameBuffer));
        fullFrameDirty = false;
    }
}

void TerminalRenderer::paintCell(int row, int col, char ch, uint8_t attr) {
    const uint16_t fg = paletteColour(VTERM_ATTR_FG(attr));
    const uint16_t bg = paletteColour(VTERM_ATTR_BG(attr));

    const int pixelY = originY + row * cellHeight - currentRowYOffset;
    const int pixelX = originX + col * cellWidth;

    font_render_char_pixel_buffer_rgb565(frameBuffer, pixelX, pixelY, &TT_TERMINAL_FONT_SYMBOL, ch, fg, bg, PIXEL_BUFFER_CONVERSION_EXACT_BLACK);
}

void TerminalRenderer::paintCursor(int row, int col) {
    if (shadow == nullptr) {
        return;
    }

    // From the shadow, not vterm's grid directly, since it reflects what render() actually drew
    // this frame (e.g. a row served from scrollback).
    const Cell& cell = shadow[row * cols + col];

    // Swapped fg/bg reads as a block cursor over whatever is there.
    const uint8_t inverted = VTERM_ATTR(VTERM_ATTR_BG(cell.attr), VTERM_ATTR_FG(cell.attr));
    paintCell(row, col, cell.ch, inverted);
}

void TerminalRenderer::render(bool force) {
    const vterm_cell_t* cells = vterm_get_direct_buffer();
    if (cells == nullptr || frameBuffer == nullptr || shadow == nullptr) {
        return;
    }

    // vterm tracks whether the active VT's cells or cursor changed since the last call; when
    // neither that nor a forced repaint applies, rows the cursor doesn't touch skip the
    // comparison scan below entirely.
    const bool textDirty = force || vterm_take_dirty();

    // Where to undo the cursor, if drawn: its position last frame, captured before this frame's
    // blink/move below can change it. Its attributes were never written to the shadow, so
    // leaving it would strand an inverted block that always compares equal.
    const bool undrawCursor = cursorDrawn && cursorRow >= 0 && cursorRow < rowCount && cursorCol >= 0 && cursorCol < cols;
    const int undrawRow = cursorRow;
    const int undrawCol = cursorCol;

    // Advance the blink on wall-clock time, so the rate doesn't depend on how often render() runs.
    const uint32_t now = get_micros_since_boot() / 1000;
    if (now - lastBlinkMs >= CURSOR_BLINK_INTERVAL_MS) {
        lastBlinkMs = now;
        blinkOn = !blinkOn;
    }

    int redrawCol = 0;
    int redrawRow = 0;
    int visible = 0;
    vterm_get_cursor(vterm_get_active(), &redrawCol, &redrawRow, &visible);

    // Typing should not make the cursor flicker off mid-keystroke, so restart the blink cycle
    // whenever it moves: the cursor is then solid for a full interval at its new position.
    if (redrawRow != cursorRow || redrawCol != cursorCol) {
        cursorRow = redrawRow;
        cursorCol = redrawCol;
        blinkOn = true;
        lastBlinkMs = now;
    }

    // No cursor while looking at history: it marks where typing goes, which is on the live screen.
    if (Scrollback::offset() > 0) {
        visible = 0;
    }

    const bool redrawCursor = visible && blinkOn && redrawRow >= 0 && redrawRow < rowCount && redrawCol >= 0 && redrawCol < cols;

    if (!textDirty && !undrawCursor && !redrawCursor) {
        return;
    }

    // Scratch row used when a line comes from scrollback rather than from the live terminal.
    vterm_cell_t historyRow[VTERM_COLS];

    for (int row = 0; row < rowCount; row++) {
        const bool touchesCursor = (undrawCursor && undrawRow == row) || (redrawCursor && redrawRow == row);
        if (!textDirty && !touchesCursor) {
            continue;
        }

        // While the view is scrolled back, the upper rows are served from history and the rest
        // still come from the live screen, so the two meet seamlessly mid-scroll.
        const bool fromHistory = Scrollback::rowForDisplay(row, rowCount, historyRow);
        const vterm_cell_t* source = fromHistory ? historyRow : &cells[row * VTERM_COLS];

        bool rowChanged = force || touchesCursor;
        if (!rowChanged) {
            // Cheap comparison-only pass: an idle row costs just this, no painting.
            for (int col = 0; col < cols; col++) {
                const Cell& previous = shadow[row * cols + col];
                if (source[col].ch != previous.ch || source[col].attr != previous.attr) {
                    rowChanged = true;
                    break;
                }
            }
        }
        if (!rowChanged) {
            continue;
        }

        currentRowYOffset = originY + row * cellHeight;

        if (undrawCursor && undrawRow == row) {
            const vterm_cell_t& under = cells[undrawRow * VTERM_COLS + undrawCol];
            paintCell(undrawRow, undrawCol, under.ch, under.attr);
            cursorDrawn = false;
        }

        // frameBuffer is reused for every row, so every cell must be repainted here, not just the
        // ones that differ from shadow, or an unpainted cell presents the previous row's pixels.
        for (int col = 0; col < cols; col++) {
            const char ch = source[col].ch;
            const uint8_t attr = source[col].attr;
            paintCell(row, col, ch, attr);
            shadow[row * cols + col] = Cell { ch, attr };
        }

        if (redrawCursor && redrawRow == row) {
            paintCursor(redrawRow, redrawCol);
            cursorDrawn = true;
        }

        present(currentRowYOffset, currentRowYOffset + cellHeight);
    }

    finishFrame();
}
