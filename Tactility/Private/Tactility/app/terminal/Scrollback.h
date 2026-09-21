#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Scrollback history for the terminal.
 *
 * vterm keeps only what is on screen: when a line scrolls off the top it is discarded, so long
 * output cannot be reviewed. This captures those lines in a ring buffer and can paint them over the
 * live screen while the user scrolls back.
 *
 * The lines are stored as vterm cells so that colour is preserved, and the buffer lives in PSRAM -
 * a few hundred lines of an 80-column terminal is far too much for internal RAM.
 */
namespace Scrollback {

/** Allocates the history buffer. Returns false if it could not be allocated. */
bool begin(int columns, int lineCount);

/** Releases the history buffer. */
void end();

/**
 * Captures the top line of the terminal, which is about to be scrolled away.
 *
 * Must be called immediately before vterm scrolls, while that line is still present.
 */
void captureTopLine();

/** Number of lines currently held. */
int storedLines();

/** How far back the view is scrolled, in lines. Zero means the live screen. */
int offset();

/**
 * Moves the view. Positive scrolls back into history, negative returns towards the present.
 * Clamped to the available history.
 * @return true if the offset changed, so the caller knows to redraw
 */
bool scroll(int lines);

/** Returns to the live screen. Returns true if the view moved. */
bool reset();

/**
 * Fills `out` with the row that should appear at screen row `screenRow` given the current offset.
 *
 * @param[out] out receives `columns` cells
 * @return false when that row comes from the live terminal rather than from history
 */
bool rowForDisplay(int screenRow, int screenRows, void* out);

} // namespace Scrollback

/** C entry point for vterm's scroll hook; calls Scrollback::captureTopLine(). */
extern "C" void scrollback_capture_top_line();
