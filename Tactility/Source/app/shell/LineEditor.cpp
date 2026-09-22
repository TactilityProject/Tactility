#include <Tactility/app/shell/LineEditor.h>

#include <Tactility/app/shell/Shell.h>
#include <Tactility/app/shell/ShellFs.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace {

// Straight to this app's own fd 1 rather than through Shell::print()/fwrite(): the prompt, echo
// and cursor movement belong to the terminal itself and must never end up in a command's redirect
// target. sh_redir.c only ever swaps the stdout/stderr/stdin FILE*, never fd 1 itself, which is
// always piped to the terminal app running it. fwrite() would also miss this app's own stdio
// wrapping entirely (see stdio_wrap.cpp), leaving nothing for that pipe to carry.
void write(const char* text, size_t length) {
    const char* bytes = text;
    size_t remaining = length;
    while (remaining > 0) {
        const ssize_t written = ::write(STDOUT_FILENO, bytes, remaining);
        if (written <= 0) {
            break;
        }
        bytes += written;
        remaining -= static_cast<size_t>(written);
    }
}

void write(const char* text) {
    write(text, strlen(text));
}

int g_terminalColumns = 80;

} // namespace

void LineEditor::setTerminalColumns(int columns) {
    g_terminalColumns = (columns > 0) ? columns : 80;
}

int LineEditor::terminalColumns() const {
    return g_terminalColumns;
}

/** Screen columns a string occupies, ignoring SGR escape sequences, which are zero-width. */
static size_t printableWidth(const char* text) {
    size_t width = 0;
    for (const char* p = text; *p != '\0'; p++) {
        if (*p == '\x1B') {
            // Skip to the end of the sequence: a CSI runs to a byte in @..~.
            p++;
            if (*p == '[') {
                while (p[1] != '\0' && !(p[1] >= '@' && p[1] <= '~')) {
                    p++;
                }
                if (p[1] != '\0') {
                    p++;
                }
            }
            continue;
        }
        width++;
    }
    return width;
}

void LineEditor::begin(const char* newPrompt) {
    prompt = newPrompt;
    promptWidth = printableWidth(newPrompt);
    buffer[0] = '\0';
    length = 0;
    cursor = 0;
    historyIndex = -1;
    escapeState = EscapeState::None;
    // A fresh prompt occupies one row; carrying the previous line's count over would make the next
    // refresh erase rows that belong to output above.
    drawnRows = 1;
    drawnCursorRow = 0;
    write(prompt);
}

void LineEditor::refresh() {
    const int columns = terminalColumns();
    // Columns, not bytes: a coloured prompt carries escapes that take no screen space.
    const size_t promptLength = promptWidth;

    // A prompt plus a long line wraps across several screen rows. "\r" only returns to the start of
    // the current row and CSI K only erases that row, so redrawing from there would leave the
    // earlier rows in place and push a fresh copy down the screen: one duplicate per keystroke.
    //
    // The cursor is therefore walked up to the row where the line starts, and every row it occupies
    // is cleared before reprinting.
    const int previousRows = (drawnRows > 0) ? drawnRows : 1;

    // Walk up from where the cursor physically is, which is not necessarily where the new buffer
    // says it should be: backspace shortens the line before calling this, so deriving the row from
    // the updated cursor would climb one row too few and leave the first row untouched.
    const int cursorRow = (drawnCursorRow < previousRows) ? drawnCursorRow : previousRows - 1;

    write("\r");
    for (int i = 0; i < cursorRow; i++) {
        write("\x1B[A");  // up one row
    }

    for (int i = 0; i < previousRows; i++) {
        write("\x1B[K");  // erase this row
        if (i + 1 < previousRows) {
            write("\x1B[B");  // down one row
        }
    }
    // Back to the first row of the line.
    for (int i = 0; i + 1 < previousRows; i++) {
        write("\x1B[A");
    }
    write("\r");

    write(prompt);
    write(buffer, length);

    /*
     * Remember how many rows this line now occupies, so the next refresh knows what to erase.
     *
     * Exactly `columns` characters occupy one row, not two: vterm defers its wrap, so the cursor
     * is still on the last column of the first row and nothing has been written below it. Counting
     * the extra row made refresh() erase and reprint a line that was not there.
     */
    const size_t total = promptLength + length;
    drawnRows = (total == 0) ? 1 : static_cast<int>((total - 1) / columns) + 1;

    /*
     * No nudge is needed to make an exactly-full line wrap.
     *
     * vterm defers the wrap: the cursor stays on the last column of a full row until another
     * character arrives, so nothing needs to force it onto a row that is not really there yet.
     */

    // Put the cursor back where it belongs if it isn't at the end.
    if (cursor < length) {
        const size_t target = promptLength + cursor;
        const size_t end = promptLength + length;
        const int targetRow = static_cast<int>(target / columns);

        /* Where the cursor physically is after printing the line. With the wrap deferred, text
         * ending exactly at the row edge leaves the cursor on that row rather than the next, so
         * this has to match the drawnRows calculation above. */
        const int endRow = (end == 0) ? 0 : static_cast<int>((end - 1) / columns);

        for (int i = targetRow; i < endRow; i++) {
            write("\x1B[A");
        }

        /*
         * Return to column 0, then step right to the target column.
         *
         * The step is omitted when the target *is* column 0: "ESC[0C" does not mean "move zero
         * columns" (a missing or zero parameter means one, per the standard), so emitting it
         * would move the cursor one cell too far, making it skip a character either side of a line
         * wrap and need two presses to cross it.
         */
        const unsigned targetColumn = static_cast<unsigned>(target % columns);
        if (targetColumn == 0) {
            write("\r");
        } else {
            char move[24];
            const int written = snprintf(move, sizeof(move), "\r\x1B[%uC", targetColumn);
            if (written > 0) {
                write(move, static_cast<size_t>(written));
            }
        }
        drawnCursorRow = targetRow;
    } else {
        drawnCursorRow = drawnRows - 1;
    }
}

void LineEditor::complete() {
    // Completion appends to the end of the line, so it only makes sense with the cursor there.
    if (cursor != length) {
        return;
    }

    char suffix[ShellFs::MAX_PATH];
    bool listed = false;
    if (!Shell::complete(buffer, suffix, sizeof(suffix), &listed)) {
        return;
    }

    for (const char* c = suffix; *c != '\0' && length + 1 < MAX_LINE; c++) {
        buffer[length++] = *c;
    }
    buffer[length] = '\0';
    cursor = length;

    // Candidates were printed over the prompt, so it has to be drawn again either way.
    if (listed || suffix[0] != '\0') {
        refresh();
    }
}

void LineEditor::insert(char c) {
    if (length + 1 >= MAX_LINE) {
        return;
    }

    // Shift the tail right to make room, so typing mid-line inserts rather than overwrites.
    memmove(&buffer[cursor + 1], &buffer[cursor], length - cursor);
    buffer[cursor] = c;
    length++;
    cursor++;
    buffer[length] = '\0';

    if (cursor == length) {
        // Appending at the end is the common case and needs no redraw: the terminal advances the
        // cursor itself, wrapping to the next row when it runs out of width. drawnRows has to
        // follow that, or a later refresh() would erase fewer rows than the line occupies.
        write(&c, 1);
        const size_t total = promptWidth + length;
        drawnRows = (total == 0) ? 1 : static_cast<int>((total - 1) / terminalColumns()) + 1;
        drawnCursorRow = drawnRows - 1;
    } else {
        refresh();
    }
}

void LineEditor::backspace() {
    if (cursor == 0) {
        return;
    }
    memmove(&buffer[cursor - 1], &buffer[cursor], length - cursor);
    length--;
    cursor--;
    buffer[length] = '\0';
    refresh();
}

void LineEditor::replaceBuffer(const char* text) {
    snprintf(buffer, sizeof(buffer), "%s", text);
    length = strlen(buffer);
    cursor = length;
    refresh();
}

void LineEditor::pushHistory(const char* line) {
    if (line[0] == '\0') {
        return;
    }
    // Skip consecutive duplicates, which is what makes Up useful after repeating a command.
    if (historyCount > 0 && strcmp(history[(historyCount - 1) % HISTORY_SIZE], line) == 0) {
        return;
    }
    snprintf(history[historyCount % HISTORY_SIZE], MAX_LINE, "%s", line);
    historyCount++;
}

void LineEditor::historyPrevious() {
    if (historyCount == 0) {
        return;
    }

    const int available = (historyCount < HISTORY_SIZE) ? historyCount : HISTORY_SIZE;
    if (historyIndex + 1 >= available) {
        return;
    }
    historyIndex++;
    replaceBuffer(history[(historyCount - 1 - historyIndex) % HISTORY_SIZE]);
}

void LineEditor::historyNext() {
    if (historyIndex < 0) {
        return;
    }
    historyIndex--;
    if (historyIndex < 0) {
        replaceBuffer("");
    } else {
        replaceBuffer(history[(historyCount - 1 - historyIndex) % HISTORY_SIZE]);
    }
}

bool LineEditor::feed(char c, const char** outLine) {
    // Arrow keys arrive as ESC [ A..D, fed one byte at a time.
    switch (escapeState) {
        case EscapeState::Escape:
            escapeState = (c == '[') ? EscapeState::Bracket : EscapeState::None;
            return false;

        case EscapeState::Bracket:
            escapeState = EscapeState::None;
            switch (c) {
                case 'A': historyPrevious(); break;
                case 'B': historyNext(); break;
                case 'C':
                    if (cursor < length) {
                        cursor++;
                        // Redrawn rather than emitting a bare cursor-right: at the end of a row
                        // ESC[C does not wrap onto the next line, so on a wrapped line the cursor
                        // would stop following the text. refresh() places it by row and column.
                        refresh();
                    }
                    break;
                case 'D':
                    if (cursor > 0) {
                        cursor--;
                        refresh();
                    }
                    break;
                default: break;
            }
            return false;

        case EscapeState::None:
            break;
    }

    switch (c) {
        case 0x1B: // ESC
            escapeState = EscapeState::Escape;
            return false;

        case '\r':
        case '\n':
            write("\r\n");
            buffer[length] = '\0';
            pushHistory(buffer);
            *outLine = buffer;
            return true;

        case '\t':
            complete();
            return false;

        case 0x7F: // DEL, which is what the keyboard driver reports for Backspace
        case 0x08: // Ctrl+H
            backspace();
            return false;

        case 0x01: // Ctrl+A: start of line
            cursor = 0;
            refresh();
            return false;

        case 0x05: // Ctrl+E: end of line
            cursor = length;
            refresh();
            return false;

        case 0x0B: // Ctrl+K: erase to end of line
            length = cursor;
            buffer[length] = '\0';
            refresh();
            return false;

        case 0x15: // Ctrl+U: erase whole line
            length = 0;
            cursor = 0;
            buffer[0] = '\0';
            refresh();
            return false;

        case 0x03: // Ctrl+C: abandon this line without running it
            write("^C\r\n");
            buffer[0] = '\0';
            length = 0;
            cursor = 0;
            historyIndex = -1;
            write(prompt);
            return false;

        case 0x0C: // Ctrl+L: clear screen and redraw
            write("\x1B[2J\x1B[H");
            refresh();
            return false;

        default:
            if (static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F) {
                insert(c);
            }
            return false;
    }
}
