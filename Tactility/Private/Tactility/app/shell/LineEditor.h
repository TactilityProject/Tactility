#pragma once

#include <cstddef>

/**
 * A line editor for the shell, with history and cursor movement.
 *
 * This replaces linenoise rather than vendoring it. linenoise drives a real tty through termios and
 * raw file descriptors; here input arrives one byte at a time over this app instance's own stdin
 * (fed by the terminal app running it), and output goes out over stdout the same way any other
 * program's output does. Reimplementing against plain stdio directly is both smaller and avoids
 * emulating a termios layer that was never there.
 *
 * Editing keys: Left/Right, Home/End (Ctrl+A / Ctrl+E), Backspace, Ctrl+U (clear line),
 * Ctrl+K (clear to end), Ctrl+C (abandon line), Ctrl+L (clear screen), Up/Down (history).
 */
class LineEditor {
public:
    static constexpr size_t MAX_LINE = 256;
    static constexpr int HISTORY_SIZE = 16;

    /** Terminal width in columns to assume, since there is no ioctl(TIOCGWINSZ) here. Call once at
     * startup with whatever the terminal app passed on the command line. */
    static void setTerminalColumns(int columns);

    /** Draws the prompt and resets the edit buffer. */
    void begin(const char* prompt);

    /**
     * Feeds one input byte.
     * @param[out] outLine the completed line, valid until the next begin()
     * @return true when Enter completed a line
     */
    bool feed(char c, const char** outLine);

    /** Redraws the prompt and current buffer, e.g. after the screen is cleared. */
    void refresh();

private:
    void complete();

    /** Terminal width in columns, used to work out how many rows the line occupies. */
    int terminalColumns() const;
    void insert(char c);
    void backspace();
    void historyPrevious();
    void historyNext();
    void replaceBuffer(const char* text);
    void pushHistory(const char* line);

    const char* prompt = "$ ";

    /*
     * Printable width of the prompt, which is not strlen() once it carries colour: SGR escapes take
     * bytes but no screen columns, and every wrap calculation here is in columns. Set by begin().
     */
    size_t promptWidth = 2;

    char buffer[MAX_LINE] = {};
    size_t length = 0;
    size_t cursor = 0;

    // Screen rows the line currently occupies, so refresh() knows how many to erase when the line
    // wraps. Without this only the last row was cleared and each redraw left a copy behind.
    int drawnRows = 1;

    // Which of those rows the cursor is physically sitting on. This tracks the screen rather than
    // the buffer: an edit updates the buffer first, so the cursor's real position is still wherever
    // the previous draw left it until refresh() moves it.
    int drawnCursorRow = 0;

    char history[HISTORY_SIZE][MAX_LINE] = {};
    int historyCount = 0;
    // -1 means "editing a new line"; otherwise an index into history, counting back from newest.
    int historyIndex = -1;

    // Escape-sequence state: arrow keys arrive as ESC [ A .. ESC [ D.
    enum class EscapeState {
        None,
        Escape,
        Bracket
    };
    EscapeState escapeState = EscapeState::None;
};
