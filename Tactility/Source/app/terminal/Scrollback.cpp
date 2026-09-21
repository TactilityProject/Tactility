#include <Tactility/app/terminal/Scrollback.h>

#include <tactility/log.h>
#include <tactility/memory.h>

#include <cstring>

extern "C" {
#include <Tactility/app/terminal/vterm/vterm.h>
}

constexpr auto* TAG = "Scrollback";

namespace {

vterm_cell_t* buffer = nullptr;
int bufferColumns = 0;
int bufferLines = 0;

// Ring buffer: `head` is where the next captured line goes, `count` how many are valid.
int head = 0;
int count = 0;

// How far back the view is scrolled. 0 = live screen.
int viewOffset = 0;

/** Returns the stored line `age` lines back from the most recent, or null if out of range. */
const vterm_cell_t* storedLine(int age) {
    if (age < 0 || age >= count) {
        return nullptr;
    }
    // head points one past the newest, so the newest is at head-1.
    int index = head - 1 - age;
    while (index < 0) {
        index += bufferLines;
    }
    return &buffer[index * bufferColumns];
}

} // namespace

namespace Scrollback {

bool begin(int columns, int lineCount) {
    end();

    if (columns <= 0 || lineCount <= 0) {
        return false;
    }

    const MemoryPolicy policy { .required = MEMORY_CAPABILITY_EXTERNAL, .desired = 0, .alignment = 0 };
    const size_t cellCount = static_cast<size_t>(columns) * lineCount;
    buffer = static_cast<vterm_cell_t*>(memory_calloc_with_policy(cellCount, sizeof(vterm_cell_t), &policy));
    if (buffer == nullptr) {
        LOG_W(TAG, "Could not allocate %u bytes of history", (unsigned)(cellCount * sizeof(vterm_cell_t)));
        return false;
    }

    bufferColumns = columns;
    bufferLines = lineCount;
    head = 0;
    count = 0;
    viewOffset = 0;
    return true;
}

void end() {
    if (buffer != nullptr) {
        memory_free(buffer);
        buffer = nullptr;
    }
    bufferColumns = 0;
    bufferLines = 0;
    head = 0;
    count = 0;
    viewOffset = 0;
}

void captureTopLine() {
    if (buffer == nullptr) {
        return;
    }

    const vterm_cell_t* cells = vterm_get_direct_buffer();
    if (cells == nullptr) {
        return;
    }

    // Row 0 is the one vterm is about to discard.
    const int columns = (bufferColumns < VTERM_COLS) ? bufferColumns : VTERM_COLS;
    memcpy(&buffer[head * bufferColumns], cells, static_cast<size_t>(columns) * sizeof(vterm_cell_t));

    head = (head + 1) % bufferLines;
    if (count < bufferLines) {
        count++;
    }

    // Keep the visible window anchored to the same text as new lines arrive, so output scrolling
    // past does not drag the view along with it.
    if (viewOffset > 0 && viewOffset < count) {
        viewOffset++;
    }
}

int storedLines() {
    return count;
}

int offset() {
    return viewOffset;
}

bool scroll(int lines) {
    const int previous = viewOffset;

    viewOffset += lines;
    if (viewOffset < 0) {
        viewOffset = 0;
    }
    if (viewOffset > count) {
        viewOffset = count;
    }

    return viewOffset != previous;
}

bool reset() {
    if (viewOffset == 0) {
        return false;
    }
    viewOffset = 0;
    return true;
}

bool rowForDisplay(int screenRow, int screenRows, void* out) {
    if (buffer == nullptr || viewOffset <= 0) {
        return false;
    }

    // With the view scrolled back by N, screen row 0 shows the line N rows above the live screen's
    // top. Rows past the end of history fall through to the live terminal.
    const int age = viewOffset - screenRow;
    if (age <= 0) {
        return false;
    }

    const vterm_cell_t* line = storedLine(age - 1);
    if (line == nullptr) {
        return false;
    }

    (void)screenRows;
    memcpy(out, line, static_cast<size_t>(bufferColumns) * sizeof(vterm_cell_t));
    return true;
}

} // namespace Scrollback

/**
 * C entry point for vterm's scroll hook.
 *
 * vterm takes a plain function pointer from C code, so the C++ member cannot be handed over
 * directly - this thin wrapper gives it the linkage it needs.
 */
extern "C" void scrollback_capture_top_line() {
    Scrollback::captureTopLine();
}
