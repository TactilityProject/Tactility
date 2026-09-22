/*
* vterm.c - Virtual Terminal Manager (Zero Copy / Swap Architecture)
* 
* Memory Architecture:
* - s_iram_buffer: 9.5KB buffer in fast Internal RAM. The Display reads this.
* - cells: Pointer in vterm_t. Points to s_iram_buffer (Active) or storage_cells (Inactive).
* - storage_cells: PSRAM buffers for inactive VTs.
* 
* On Switch:
* 1. Copy s_iram_buffer -> old_vt->storage_cells (Save state)
* 2. Copy new_vt->storage_cells -> s_iram_buffer (Load state)
*/

#include <Tactility/app/terminal/vterm/vterm.h>
#include <tactility/freertos/freertos.h>
#include <tactility/freertos/queue.h>
#include <tactility/freertos/semphr.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/memory.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define INPUT_QUEUE_SIZE    64
#define BUFFER_SIZE_BYTES   (VTERM_ROWS * VTERM_COLS * sizeof(vterm_cell_t))

// The single "Hot" buffer used by the display and the active writer
// Must be 32-bit aligned for the optimized renderer
static vterm_cell_t* s_iram_buffer = NULL;

typedef struct {
    // If this VT is active, this points to s_iram_buffer.
    // If inactive, this points to storage_cells.
    vterm_cell_t *cells;

    // Backing store in PSRAM (holds state when VT is not active)
    vterm_cell_t *storage_cells;

    int cursor_x;
    int cursor_y;
    int cursor_visible;    // 1 = show, 0 = hidden (DECTCEM)
    uint8_t current_attr;  // 4-bit fg + 4-bit bg

    QueueHandle_t input_queue;
    SemaphoreHandle_t mutex;

    // Escape parsing
    int escape_state;
    char escape_buf[32];
    int escape_len;

    /*
     * Deferred wrap, as a real terminal does it.
     *
     * Writing the last column of a row used to move the cursor to the next line immediately, which
     * meant filling the bottom-right cell scrolled the screen - so anything drawing a full-width
     * bar on the last row had to leave that cell blank, and the gap was visible.
     *
     * Instead the cursor stays put and this is set. The wrap only happens if another printable
     * character actually arrives, so a bar that ends exactly at the edge never scrolls anything.
     */
    int pending_wrap;

} vterm_t;

static vterm_t *s_vterms = NULL;
volatile int s_active_vt = 0;

/* Tactility: called with the top line still intact, just before it scrolls off. Lets the
 * shell keep a scrollback history without vterm having to own one. */
static void (*s_scroll_callback)(void) = NULL;

void vterm_set_scroll_callback(void (*callback)(void)) {
    s_scroll_callback = callback;
}

static void (*s_on_switch_cb)(int new_vt) = NULL;

/*
 * How many rows the writing task should treat as the bottom of the screen.
 *
 * VTERM_ROWS is the compiled-in buffer height - sized for the largest panel this firmware might
 * run on, not necessarily this one. vterm_set_size_override() tells vterm the real, usually
 * smaller, drawable size for the calling task; scroll-triggering logic must consult this rather
 * than VTERM_ROWS directly, or a panel with fewer rows never scrolls until the cursor reaches row
 * VTERM_ROWS - by which point far more than a screenful has already been written off the bottom,
 * invisibly, with nothing to show for it.
 */
static int effective_rows(void) {
    int rows;
    vterm_get_size(&rows, NULL);
    return rows;
}

/* Same idea as effective_rows(), but for columns: wrap-triggering logic must consult this rather
 * than VTERM_COLS directly, or a panel with fewer columns never wraps until the cursor reaches
 * column VTERM_COLS - by which point text has been written off the right edge of the visible
 * grid, invisibly, instead of wrapping onto the next line. */
static int effective_cols(void) {
    int cols;
    vterm_get_size(NULL, &cols);
    return cols;
}

// Forward declarations
static void vterm_clear_internal(vterm_t *vt);
void vterm_send_input(int vt_id, char c);

// ============ Internal Functions ============

// Scroll the entire screen up by 1 line
static void vterm_scroll(vterm_t *vt)
{
    /* Tactility: hand the top line to the scrollback history before it is overwritten. This is the
     * only point at which a line is lost, so it is the only hook needed. The callback is optional -
     * vterm works exactly as before when none is installed. */
    if (vt->cells == vterm_get_direct_buffer() && s_scroll_callback) {
        s_scroll_callback();
    }

    const int rows = effective_rows();

    // Move lines 1..N-1 to 0..N-2
    // Calculate size of (ROWS - 1) lines
    size_t block_size = (rows - 1) * VTERM_COLS * sizeof(vterm_cell_t);

    // memmove is safe for overlapping regions
    memmove(&vt->cells[0], &vt->cells[VTERM_COLS], block_size);

    // Clear last line
    vterm_cell_t *last_line = &vt->cells[(rows - 1) * VTERM_COLS];
    for (int x = 0; x < VTERM_COLS; x++) {
        last_line[x].ch = ' ';
        last_line[x].attr = VTERM_DEFAULT_ATTR;
    }
    vt->cursor_y = rows - 1;
}

static void vterm_putchar_internal(vterm_t *vt, char c)
{
    /*
     * Resolve a wrap left pending by the previous character.
     *
     * Only a printable character actually causes the wrap - a newline, carriage return or escape
     * sequence supersedes it, which is exactly why the wrap is deferred rather than done when the
     * last column was written.
     */
    if (vt->pending_wrap && c >= 32 && c < 127) {
        vt->pending_wrap = 0;
        vt->cursor_x = 0;
        vt->cursor_y++;
        if (vt->cursor_y >= effective_rows()) vterm_scroll(vt);
    }

    // Direct pointer access for speed
    vterm_cell_t *cell = &vt->cells[vt->cursor_y * VTERM_COLS + vt->cursor_x];

    switch (c) {
    case '\n':
        vt->pending_wrap = 0;
        vt->cursor_x = 0;
        vt->cursor_y++;
        if (vt->cursor_y >= effective_rows()) vterm_scroll(vt);
        break;
    case '\r':
        vt->pending_wrap = 0;
        vt->cursor_x = 0;
        break;
    case '\b':
        vt->pending_wrap = 0;
        if (vt->cursor_x > 0) {
            vt->cursor_x--;
            // Backspace erases visually
            cell--; // Move pointer back
            cell->ch = ' ';
            cell->attr = vt->current_attr;
        }
        break;
    case '\t':
        vt->pending_wrap = 0;
        do {
            cell->ch = ' ';
            cell->attr = vt->current_attr;
            cell++;
            vt->cursor_x++;
        } while (vt->cursor_x < effective_cols() && (vt->cursor_x % 8) != 0);
        if (vt->cursor_x >= effective_cols()) {
            // Park at the last column with the wrap deferred, rather than moving off the row.
            vt->cursor_x = effective_cols() - 1;
            vt->pending_wrap = 1;
        }
        break;
    default:
        if (c >= 32 && c < 127) {
            cell->ch = c;
            cell->attr = vt->current_attr;
            if (vt->cursor_x + 1 >= effective_cols()) {
                // Last column written: stay here and wrap only if something else arrives.
                vt->pending_wrap = 1;
            } else {
                vt->cursor_x++;
            }
        }
        break;
    }
}

static void vterm_clear_internal(vterm_t *vt)
{
    vterm_cell_t *p = vt->cells;
    vterm_cell_t *end = p + (VTERM_ROWS * VTERM_COLS);

    // Fill optimization: Construct a 32-bit pattern of two cells
    uint16_t fill = (VTERM_DEFAULT_ATTR << 8) | ' ';
    uint32_t fill32 = (fill << 16) | fill;

    // Align to 32-bit
    while ((uintptr_t)p & 3 && p < end) {
        p->ch = ' '; p->attr = VTERM_DEFAULT_ATTR; p++;
    }
    uint32_t *p32 = (uint32_t *)p;
    while (p32 < (uint32_t *)end) {
        *p32++ = fill32;
    }
    // Handle remaining
    p = (vterm_cell_t *)p32;
    while (p < end) {
        p->ch = ' '; p->attr = VTERM_DEFAULT_ATTR; p++;
    }

    vt->cursor_x = 0;
    vt->cursor_y = 0;
    vt->cursor_visible = 1;  // Cursor visible by default
    vt->current_attr = VTERM_DEFAULT_ATTR;
    vt->pending_wrap = 0;
}

// Helper to parse a number from SGR params, advancing pointer
static int sgr_parse_num(const char **pp)
{
    int num = 0;
    while (**pp >= '0' && **pp <= '9') {
        num = num * 10 + (**pp - '0');
        (*pp)++;
    }
    if (**pp == ';') (*pp)++;
    return num;
}

static void vterm_apply_sgr(vterm_t *vt, const char *params)
{
    const char *p = params;
    int bright = 0;

    // Handle empty or "0" reset
    if (*p == '\0' || (*p == '0' && (p[1] == '\0' || p[1] == ';'))) {
        vt->current_attr = VTERM_DEFAULT_ATTR;
        if (*p == '0') p++;
        if (*p == ';') p++;
    }

    while (*p) {
        int num = sgr_parse_num(&p);

        // --- OPTIMIZATION: Check most common codes (Colors) first ---
        
        if (num >= 90 && num <= 97) {
            // Bright foreground colors (8-15)
            uint8_t fg = (num - 90) | VTERM_BRIGHT;
            uint8_t bg = VTERM_ATTR_BG(vt->current_attr);
            vt->current_attr = VTERM_ATTR(fg, bg);
        } 
        else if (num >= 30 && num <= 37) {
            // Standard foreground colors (0-7)
            uint8_t fg = (num - 30) | bright;
            uint8_t bg = VTERM_ATTR_BG(vt->current_attr);
            vt->current_attr = VTERM_ATTR(fg, bg);
        }
        else if (num == 0) {
            vt->current_attr = VTERM_DEFAULT_ATTR;
            bright = 0;
        } 
        // --- End Optimization ---
        
        else if (num == 1) {
            bright = VTERM_BRIGHT;
            uint8_t fg = VTERM_ATTR_FG(vt->current_attr);
            uint8_t bg = VTERM_ATTR_BG(vt->current_attr);
            vt->current_attr = VTERM_ATTR(fg | VTERM_BRIGHT, bg);
        } else if (num == 22) {
            bright = 0;
            uint8_t fg = VTERM_ATTR_FG(vt->current_attr) & 0x07;
            uint8_t bg = VTERM_ATTR_BG(vt->current_attr);
            vt->current_attr = VTERM_ATTR(fg, bg);
        } else if (num == 38) {
            // Extended foreground: 38;5;N (256-color) or 38;2;R;G;B (truecolor)
            // Gracefully skip - we only support 16-color mode
            int mode = sgr_parse_num(&p);
            if (mode == 5) {
                sgr_parse_num(&p);  // Skip color index
            } else if (mode == 2) {
                sgr_parse_num(&p); sgr_parse_num(&p); sgr_parse_num(&p);  // Skip R, G, B
            }
        } else if (num == 39) {
            // Default foreground
            uint8_t bg = VTERM_ATTR_BG(vt->current_attr);
            vt->current_attr = VTERM_ATTR(VTERM_WHITE, bg);
        } else if (num >= 40 && num <= 47) {
            // Standard background colors (0-7)
            uint8_t fg = VTERM_ATTR_FG(vt->current_attr);
            uint8_t bg = num - 40;
            vt->current_attr = VTERM_ATTR(fg, bg);
        } else if (num == 48) {
            // Extended background: 48;5;N (256-color) or 48;2;R;G;B (truecolor)
            // Gracefully skip - we only support 16-color mode
            int mode = sgr_parse_num(&p);
            if (mode == 5) {
                sgr_parse_num(&p);  // Skip color index
            } else if (mode == 2) {
                sgr_parse_num(&p); sgr_parse_num(&p); sgr_parse_num(&p);  // Skip R, G, B
            }
        } else if (num == 49) {
            // Default background
            uint8_t fg = VTERM_ATTR_FG(vt->current_attr);
            vt->current_attr = VTERM_ATTR(fg, VTERM_BLACK);
        } else if (num >= 100 && num <= 107) {
            // Bright background colors (8-15)
            uint8_t fg = VTERM_ATTR_FG(vt->current_attr);
            uint8_t bg = (num - 100) | VTERM_BRIGHT;
            vt->current_attr = VTERM_ATTR(fg, bg);
        }
    }
}

static int vterm_handle_escape(vterm_t *vt, char c)
{
    if (vt->escape_state == 0) {
        if (c == '\033') {
            vt->escape_state = 1;
            vt->escape_len = 0;
            return 1;
        }
        return 0;
    }

    if (vt->escape_state == 1) {
        if (c == '[') {
            vt->escape_state = 2;
            return 1;
        }
        // Non-CSI escape sequences: ESC <letter>
        if (c == 'D') {
            // IND - Index: move cursor down, scroll if at bottom
            if (vt->cursor_y >= effective_rows() - 1) {
                vterm_scroll(vt);
            } else {
                vt->cursor_y++;
            }

            vt->escape_state = 0;
            return 1;
        }
        if (c == 'M') {
            // RI - Reverse Index: move cursor up, scroll down if at top
            if (vt->cursor_y <= 0) {
                // Scroll down: move lines 0..N-2 to 1..N-1
                memmove(&vt->cells[VTERM_COLS], &vt->cells[0],
                        (effective_rows() - 1) * VTERM_COLS * sizeof(vterm_cell_t));

                // Clear top line
                vterm_cell_t *top_row = &vt->cells[0];
                for (int x = 0; x < VTERM_COLS; x++) {
                    top_row[x].ch = ' ';
                    top_row[x].attr = VTERM_DEFAULT_ATTR;
                }
            } else {
                vt->cursor_y--;
            }

            vt->escape_state = 0;
            return 1;
        }
        if (c == 'E') {
            // NEL - Next Line: move to column 1 of next line, scroll if needed
            vt->cursor_x = 0;
            if (vt->cursor_y >= effective_rows() - 1) {
                vterm_scroll(vt);
            } else {
                vt->cursor_y++;
            }

            vt->escape_state = 0;
            return 1;
        }
        vt->escape_state = 0;
        return 0;
    }

    if (vt->escape_len < (int)sizeof(vt->escape_buf) - 1) {
        vt->escape_buf[vt->escape_len++] = c;
        vt->escape_buf[vt->escape_len] = '\0';
    }

    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        vt->escape_buf[vt->escape_len - 1] = '\0';

        // DEC private mode sequences (ESC [ ? ...)
        if (vt->escape_buf[0] == '?') {
            // Parse ?25h (show cursor) and ?25l (hide cursor)
            if (c == 'h' && strcmp(vt->escape_buf, "?25") == 0) {
                vt->cursor_visible = 1;
            } else if (c == 'l' && strcmp(vt->escape_buf, "?25") == 0) {
                vt->cursor_visible = 0;
            }
            // Other DEC private modes gracefully ignored
            vt->escape_state = 0;
            vt->escape_len = 0;
            return 1;
        }

        /* An escape sequence supersedes a wrap left pending by the last character: the cursor is
         * about to be moved or the screen changed, so wrapping afterwards would be wrong. */
        vt->pending_wrap = 0;

        switch (c) {
        case 'm': vterm_apply_sgr(vt, vt->escape_buf); break;
        case 'J':
            if (strcmp(vt->escape_buf, "2") == 0 || strcmp(vt->escape_buf, "") == 0) {
                vterm_clear_internal(vt);
            }
            break;
        case 'H':
        case 'f':
            if (vt->escape_buf[0] == '\0' || strcmp(vt->escape_buf, "1;1") == 0) {
                vt->cursor_x = 0;
                vt->cursor_y = 0;
            } else {
                int row = 1, col = 1;
                sscanf(vt->escape_buf, "%d;%d", &row, &col);
                vt->cursor_y = (row > 0 ? row - 1 : 0);
                vt->cursor_x = (col > 0 ? col - 1 : 0);
                if (vt->cursor_y >= VTERM_ROWS) vt->cursor_y = VTERM_ROWS - 1;
                if (vt->cursor_x >= VTERM_COLS) vt->cursor_x = VTERM_COLS - 1;
            }
            break;
        case 'A': { // Cursor Up
            int n = 1;
            if (vt->escape_buf[0]) n = atoi(vt->escape_buf);
            if (n < 1) n = 1;
            vt->cursor_y -= n;
            if (vt->cursor_y < 0) vt->cursor_y = 0;
            break;
        }
        case 'B': { // Cursor Down
            int n = 1;
            if (vt->escape_buf[0]) n = atoi(vt->escape_buf);
            if (n < 1) n = 1;
            vt->cursor_y += n;
            if (vt->cursor_y >= VTERM_ROWS) vt->cursor_y = VTERM_ROWS - 1;
            break;
        }
        case 'C': { // Cursor Right
            int n = 1;
            if (vt->escape_buf[0]) n = atoi(vt->escape_buf);
            if (n < 1) n = 1;
            vt->cursor_x += n;
            if (vt->cursor_x >= VTERM_COLS) vt->cursor_x = VTERM_COLS - 1;
            break;
        }
        case 'D': { // Cursor Left
            int n = 1;
            if (vt->escape_buf[0]) n = atoi(vt->escape_buf);
            if (n < 1) n = 1;
            vt->cursor_x -= n;
            if (vt->cursor_x < 0) vt->cursor_x = 0;
            break;
        }
        case 'K': { // Erase in Line
            int mode = 0;
            if (vt->escape_buf[0]) mode = atoi(vt->escape_buf);
            int start = 0, end = VTERM_COLS;
            if (mode == 0) start = vt->cursor_x; // Cursor to end
            else if (mode == 1) end = vt->cursor_x + 1; // Start to cursor
            
            // Get pointer to current row
            vterm_cell_t *row = &vt->cells[vt->cursor_y * VTERM_COLS];
            for (int x = start; x < end; x++) {
                row[x].ch = ' ';
                row[x].attr = vt->current_attr;
            }
            break;
        }
        case 'X': { // Erase N Chars
            int n = 1;
            if (vt->escape_buf[0]) n = atoi(vt->escape_buf);
            if (n < 1) n = 1;
            int end = vt->cursor_x + n;
            if (end > VTERM_COLS) end = VTERM_COLS;

            vterm_cell_t *row = &vt->cells[vt->cursor_y * VTERM_COLS];
            for (int x = vt->cursor_x; x < end; x++) {
                row[x].ch = ' ';
                row[x].attr = vt->current_attr;
            }
            break;
        }
        case 'L': {
            // IL - Insert Lines: insert N blank lines at cursor row, scroll down
            int n = 1;
            if (vt->escape_buf[0]) n = atoi(vt->escape_buf);
            if (n < 1) n = 1;
            if (n > VTERM_ROWS - vt->cursor_y) n = VTERM_ROWS - vt->cursor_y;

            // Move lines down
            int lines_to_move = VTERM_ROWS - vt->cursor_y - n;
            if (lines_to_move > 0) {
                memmove(&vt->cells[(vt->cursor_y + n) * VTERM_COLS],
                        &vt->cells[vt->cursor_y * VTERM_COLS],
                        lines_to_move * VTERM_COLS * sizeof(vterm_cell_t));
            }

            // Clear inserted lines
            for (int y = vt->cursor_y; y < vt->cursor_y + n; y++) {
                vterm_cell_t *row = &vt->cells[y * VTERM_COLS];
                for (int x = 0; x < VTERM_COLS; x++) {
                    row[x].ch = ' ';
                    row[x].attr = VTERM_DEFAULT_ATTR;
                }
            }
            break;
        }
        case 'M': {
            // DL - Delete Lines: delete N lines at cursor row, scroll up
            int n = 1;
            if (vt->escape_buf[0]) n = atoi(vt->escape_buf);
            if (n < 1) n = 1;
            if (n > VTERM_ROWS - vt->cursor_y) n = VTERM_ROWS - vt->cursor_y;

            // Move lines up
            int lines_to_move = VTERM_ROWS - vt->cursor_y - n;
            if (lines_to_move > 0) {
                memmove(&vt->cells[vt->cursor_y * VTERM_COLS],
                        &vt->cells[(vt->cursor_y + n) * VTERM_COLS],
                        lines_to_move * VTERM_COLS * sizeof(vterm_cell_t));
            }

            // Clear vacated lines at bottom
            for (int y = VTERM_ROWS - n; y < VTERM_ROWS; y++) {
                vterm_cell_t *row = &vt->cells[y * VTERM_COLS];
                for (int x = 0; x < VTERM_COLS; x++) {
                    row[x].ch = ' ';
                    row[x].attr = VTERM_DEFAULT_ATTR;
                }
            }
            break;
        }
        case 'n':
            /*
             * Device Status Report. ESC[6n asks where the cursor is, and the reply goes back
             * through the input queue - the same one that carries key presses, since that is the
             * only channel back to the program.
             *
             * This is how a program discovers the screen size: drive the cursor far past the edge
             * with ESC[999;999H, which clamps, then ask where it ended up. The clamp in the 'H'
             * handler above is against the real grid, so the answer is correct by construction.
             */
            if (vt->escape_buf[0] == '6' && vt->escape_buf[1] == '\0') {
                char resp[32];
                snprintf(resp, sizeof(resp), "\x1b[%d;%dR", vt->cursor_y + 1, vt->cursor_x + 1);
                for (int i = 0; resp[i] != '\0'; i++) vterm_send_input(s_active_vt, resp[i]);
            }
            break;
        }

        vt->escape_state = 0;
        vt->escape_len = 0;
        return 1;
    }

    return 1;
}

// ============ Public API ============

// Defined below, next to flush_input_buffer(), which it guards.
static struct Mutex s_input_mux;

error_t vterm_init(void)
{
    mutex_construct(&s_input_mux);

    const struct MemoryPolicy internal_policy = { .required = MEMORY_CAPABILITY_INTERNAL, .desired = 0, .alignment = 0 };

    // 1. Allocate the shared HOT buffer in IRAM (Internal RAM)
    s_iram_buffer = (vterm_cell_t *)memory_calloc_with_policy(1, BUFFER_SIZE_BYTES, &internal_policy);
    if (!s_iram_buffer) {
        printf("Failed to allocate IRAM vterm buffer\n");
        return ERROR_OUT_OF_MEMORY;
    }

    // 2. Allocate VTs struct
    s_vterms = (vterm_t *)memory_calloc_with_policy(VTERM_COUNT, sizeof(vterm_t), &internal_policy);
    if (!s_vterms) return ERROR_OUT_OF_MEMORY;

    for (int i = 0; i < VTERM_COUNT; i++) {
        vterm_t *vt = &s_vterms[i];
        vt->input_queue = xQueueCreate(INPUT_QUEUE_SIZE, sizeof(char));
        vt->mutex = xSemaphoreCreateMutex();

#if VTERM_COUNT > 1
        // Allocate PSRAM backing store for VT switching
        const struct MemoryPolicy external_policy = { .required = MEMORY_CAPABILITY_EXTERNAL, .desired = 0, .alignment = 0 };
        vt->storage_cells = (vterm_cell_t *)memory_alloc_with_policy(BUFFER_SIZE_BYTES, &external_policy);
        if (!vt->storage_cells) {
            printf("Failed to allocate PSRAM for VT %d (PSRAM required for %d VTs)\n", i, VTERM_COUNT);
            return ERROR_OUT_OF_MEMORY;
        }

        // Default: point to storage. Switch() will fix the active one.
        vt->cells = vt->storage_cells;
#else
        // Single VT mode: no storage needed, always uses IRAM buffer
        vt->storage_cells = NULL;
        vt->cells = s_iram_buffer;
#endif

        vterm_clear_internal(vt);
    }

    // 3. Set up initial active VT (0)
    s_active_vt = 0;
#if VTERM_COUNT > 1
    // Load VT0 storage into IRAM
    memcpy(s_iram_buffer, s_vterms[0].storage_cells, BUFFER_SIZE_BYTES);
    // Point VT0 to IRAM
    s_vterms[0].cells = s_iram_buffer;
#endif
    // In single VT mode, cells already points to s_iram_buffer

    return ERROR_NONE;
}

/*
 * Frees everything vterm_init() allocates, and clears every module-global vterm_init() sets, so a
 * caller that runs once per app instance (the terminal app: vterm_init() on every launch, no guard
 * against re-entry) doesn't leak the IRAM buffer, the vterm array, or each VT's queue/mutex/PSRAM
 * storage on every relaunch.
 */
void vterm_deinit(void)
{
    if (s_vterms != NULL) {
        for (int i = 0; i < VTERM_COUNT; i++) {
            vterm_t *vt = &s_vterms[i];
            if (vt->input_queue != NULL) {
                vQueueDelete(vt->input_queue);
            }
            if (vt->mutex != NULL) {
                vSemaphoreDelete(vt->mutex);
            }
#if VTERM_COUNT > 1
            if (vt->storage_cells != NULL) {
                memory_free(vt->storage_cells);
            }
#endif
        }
        memory_free(s_vterms);
        s_vterms = NULL;
    }

    if (s_iram_buffer != NULL) {
        memory_free(s_iram_buffer);
        s_iram_buffer = NULL;
    }

    s_active_vt = 0;
    s_scroll_callback = NULL;
    s_on_switch_cb = NULL;

    vterm_clear_size_override();
    mutex_destruct(&s_input_mux);
}

vterm_cell_t *vterm_get_direct_buffer(void)
{
    return s_iram_buffer;
}

void vterm_switch(int vt_id)
{
#if VTERM_COUNT > 1
    if (vt_id < 0 || vt_id >= VTERM_COUNT) return;
    if (vt_id == s_active_vt) return;

    vterm_t *old_vt = &s_vterms[s_active_vt];
    vterm_t *new_vt = &s_vterms[vt_id];

    // Lock both to ensure no writing happens during swap
    xSemaphoreTake(old_vt->mutex, portMAX_DELAY);
    xSemaphoreTake(new_vt->mutex, portMAX_DELAY);

    // 1. Save Active State: Copy IRAM -> Old PSRAM Storage
    memcpy(old_vt->storage_cells, s_iram_buffer, BUFFER_SIZE_BYTES);
    old_vt->cells = old_vt->storage_cells; // Old now points to PSRAM

    // 2. Load New State: Copy New PSRAM Storage -> IRAM
    memcpy(s_iram_buffer, new_vt->storage_cells, BUFFER_SIZE_BYTES);
    new_vt->cells = s_iram_buffer; // New now points to IRAM

    s_active_vt = vt_id;

    xSemaphoreGive(new_vt->mutex);
    xSemaphoreGive(old_vt->mutex);

    if (s_on_switch_cb) s_on_switch_cb(vt_id);
#else
    (void)vt_id; // Single VT mode: switching disabled
#endif
}

void vterm_write(int vt_id, const char *data, size_t len)
{
    if (vt_id < 0 || vt_id >= VTERM_COUNT) return;
    vterm_t *vt = &s_vterms[vt_id];

    xSemaphoreTake(vt->mutex, portMAX_DELAY);
    const char *p = data;
    const char *end = data + len;

    // Cache state
    int cx = vt->cursor_x;
    int cy = vt->cursor_y;
    uint8_t current_attr = vt->current_attr;
    int escape_mode = vt->escape_state;
    int pending_wrap = vt->pending_wrap;

    const int cols = effective_cols();
    vterm_cell_t *cells_base = vt->cells;
    vterm_cell_t *cursor_ptr = &cells_base[cy * VTERM_COLS + cx];
    vterm_cell_t *row_end = &cells_base[cy * VTERM_COLS + cols];

    while (p < end) {
        char c = *p++;

        // Fast path: printable ASCII characters (most common case)
        if (escape_mode == 0 && c >= 32 && c < 127) {
            /*
             * Resolve a wrap deferred by the previous character before writing this one.
             *
             * The wrap is deferred so that filling the last column of a row does not move the
             * cursor off it - which, on the bottom row, scrolled the whole screen and forced
             * anything drawing a full-width bar to leave the corner cell blank.
             */
            if (pending_wrap) {
                pending_wrap = 0;
                cx = 0; cy++;
                if (cy >= effective_rows()) {
                    vt->cursor_x = cx; vt->cursor_y = cy;
                    vterm_scroll(vt);
                    cy = vt->cursor_y;
                }
                cursor_ptr = &cells_base[cy * VTERM_COLS + cx];
                row_end = &cells_base[cy * VTERM_COLS + cols];
            }

            cursor_ptr->ch = c;
            cursor_ptr->attr = current_attr;

            if (cursor_ptr + 1 >= row_end) {
                // Last column: stay here and wrap only if another printable character arrives.
                pending_wrap = 1;
            } else {
                cursor_ptr++;
                cx++;
            }
            continue;
        }

        // Slow path: escape sequences and control characters
        vt->cursor_x = cx;
        vt->cursor_y = cy;
        vt->current_attr = current_attr;
        vt->escape_state = escape_mode;
        vt->pending_wrap = pending_wrap;

        if (!vterm_handle_escape(vt, c)) {
            vterm_putchar_internal(vt, c);
        }

        escape_mode = vt->escape_state;
        current_attr = vt->current_attr;
        pending_wrap = vt->pending_wrap;

        if (vt->cursor_x != cx || vt->cursor_y != cy) {
            cx = vt->cursor_x;
            cy = vt->cursor_y;
            cursor_ptr = &cells_base[cy * VTERM_COLS + cx];
            row_end = &cells_base[cy * VTERM_COLS + cols];
        }
    }

    vt->cursor_x = cx;
    vt->cursor_y = cy;
    vt->current_attr = current_attr;
    vt->escape_state = escape_mode;
    vt->pending_wrap = pending_wrap;


    xSemaphoreGive(vt->mutex);
}

void vterm_write_translated(const char *data, size_t size)
{
    size_t start = 0;

    for (size_t i = 0; i < size; i++) {
        if (data[i] != '\n') {
            continue;
        }
        if (i > start && data[i - 1] == '\r') {
            continue; // already-paired CRLF passes through untouched
        }
        if (i > start) {
            vterm_write(s_active_vt, data + start, i - start);
        }
        vterm_write(s_active_vt, "\r\n", 2);
        start = i + 1;
    }

    if (start < size) {
        vterm_write(s_active_vt, data + start, size - start);
    }
}

// Helpers
void vterm_set_switch_callback(void (*cb)(int)) { s_on_switch_cb = cb; }
int vterm_get_active(void) { return s_active_vt; }
// Per-task size override (for SSH sessions with different terminal dimensions)
static TaskHandle_t s_size_override_task = NULL;
static int s_size_override_rows = 0;
static int s_size_override_cols = 0;

void vterm_set_size_override(int rows, int cols) {
    // Clamp to LCD size — larger terminals cause OOM on ESP32 (limited SRAM)
    if (cols > VTERM_COLS) cols = VTERM_COLS;
    if (rows > VTERM_ROWS) rows = VTERM_ROWS;
    s_size_override_rows = rows;
    s_size_override_cols = cols;
    s_size_override_task = xTaskGetCurrentTaskHandle();
}

void vterm_clear_size_override(void) {
    s_size_override_task = NULL;
}

void vterm_get_size(int *r, int *c) {
    if (s_size_override_task == xTaskGetCurrentTaskHandle()) {
        if (r) *r = s_size_override_rows;
        if (c) *c = s_size_override_cols;
        return;
    }
    if(r) *r=VTERM_ROWS;
    if(c) *c=VTERM_COLS;
}

void vterm_get_cursor(int vt_id, int *col, int *row, int *visible) {
    if (vt_id >= 0 && vt_id < VTERM_COUNT) {
        vterm_t *vt = &s_vterms[vt_id];
        if (col) *col = vt->cursor_x;
        if (row) *row = vt->cursor_y;
        if (visible) *visible = vt->cursor_visible;
    }
}

int vterm_getchar(int vt_id, int timeout_ms) {
    if (vt_id < 0 || vt_id >= VTERM_COUNT) return -1;
    char c;
    TickType_t wait = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(s_vterms[vt_id].input_queue, &c, wait) == pdTRUE) return (unsigned char)c;
    return -1;
}

void vterm_send_input(int vt_id, char c) {
    if (vt_id >= 0 && vt_id < VTERM_COUNT) xQueueSend(s_vterms[vt_id].input_queue, &c, 0);
}

void vterm_input_flush(int vt_id) {
    if (vt_id < 0 || vt_id >= VTERM_COUNT) return;
    xQueueReset(s_vterms[vt_id].input_queue);
}

// Hotkey / Input logic (Compact copy for completeness)
static char s_esc_buf[16];
static int s_esc_len = 0;
static TickType_t s_esc_start = 0;
static struct Mutex s_input_mux;

static void flush_input_buffer(void) {
    // Prevents another task from preempting mid-flush, so all chars of an escape sequence are
    // added atomically. Task context only (never called from an ISR).
    mutex_lock(&s_input_mux);
    for (int i = 0; i < s_esc_len; i++) vterm_send_input(s_active_vt, s_esc_buf[i]);
    s_esc_len = 0;
    mutex_unlock(&s_input_mux);
}

// Check if buffer matches a VT switch sequence, return VT number or -1
static int match_vt_hotkey(void)
{
    if (s_esc_len < 2) return -1;
    
    // Must start with ESC
    if (s_esc_buf[0] != '\033') return -1;
    
    // ESC O <x> - F1-F4 or Ctrl+F1-F4
    if (s_esc_len >= 3 && s_esc_buf[1] == 'O') {
        char c = s_esc_buf[2];
        // ESC O P/Q/R/S = F1-F4
        if (c == 'P') return 0;
        if (c == 'Q') return 1;
        if (c == 'R') return 2;
        if (c == 'S') return 3;
        
        // ESC O 5 P/Q/R/S = Ctrl+F1-F4 (some terminals)
        if (s_esc_len >= 4 && c == '5') {
            char d = s_esc_buf[3];
            if (d == 'P') return 0;
            if (d == 'Q') return 1;
            if (d == 'R') return 2;
            if (d == 'S') return 3;
        }
    }
    
    // ESC [ ... sequences
    if (s_esc_len >= 3 && s_esc_buf[1] == '[') {
        // Null-terminate for easier parsing
        s_esc_buf[s_esc_len] = '\0';
        
        // ESC [ 1 ; 5 P/Q/R/S = Ctrl+F1-F4 (xterm)
        if (s_esc_len == 6 && s_esc_buf[2] == '1' && s_esc_buf[3] == ';' && s_esc_buf[4] == '5') {
            char c = s_esc_buf[5];
            if (c == 'P') return 0;
            if (c == 'Q') return 1;
            if (c == 'R') return 2;
            if (c == 'S') return 3;
        }
        
        // ESC [ 11~ to [14~ = F1-F4 (vt style)
        // ESC [ 11;5~ to [14;5~ = Ctrl+F1-F4 (vt style with modifier)
        if (s_esc_buf[s_esc_len - 1] == '~') {
            int num = 0;
            
            // Parse: ESC [ <num> ~ or ESC [ <num> ; <mod> ~
            char *p = &s_esc_buf[2];
            while (*p >= '0' && *p <= '9') {
                num = num * 10 + (*p - '0');
                p++;
            }
            // Skip optional modifier (;5 etc) - we accept with or without
            if (*p == ';') {
                p++;
                while (*p >= '0' && *p <= '9') p++;
            }
            
            // F1=11, F2=12, F3=13, F4=14
            if (num >= 11 && num <= 14) {
                return num - 11;
            }
        }
        
        // ESC [ 49;5u to [52;5u = Ctrl+1-4 (CSI u / fixterms)
        // 49='1', 50='2', 51='3', 52='4'
        if (s_esc_buf[s_esc_len - 1] == 'u') {
            int codepoint = 0;
            int modifier = 0;
            
            char *p = &s_esc_buf[2];
            while (*p >= '0' && *p <= '9') {
                codepoint = codepoint * 10 + (*p - '0');
                p++;
            }
            if (*p == ';') {
                p++;
                while (*p >= '0' && *p <= '9') {
                    modifier = modifier * 10 + (*p - '0');
                    p++;
                }
            }
            
            // Modifier 5 = Ctrl
            if (modifier == 5 && codepoint >= 49 && codepoint <= 52) {
                return codepoint - 49;  // '1'->0, '2'->1, etc.
            }
        }
    }
    
    return -1;
}

// Check if we're in the middle of a potential hotkey sequence
static int could_be_hotkey(void)
{
    if (s_esc_len == 0) return 0;
    if (s_esc_buf[0] != '\033') return 0;
    if (s_esc_len == 1) return 1;  // Just ESC, wait for more
    
    char c1 = s_esc_buf[1];
    
    // ESC O ... or ESC [ ... could be hotkeys
    if (c1 == 'O' || c1 == '[') {
        // Limit max length to avoid hanging on garbage
        if (s_esc_len > 10) return 0;
        
        // Check for terminal characters
        if (s_esc_len >= 3) {
            char last = s_esc_buf[s_esc_len - 1];
            // These end a sequence
            if (last == '~' || last == 'u' || 
                (last >= 'A' && last <= 'Z') ||
                (c1 == 'O' && s_esc_len >= 3 && last >= 'P' && last <= 'S')) {
                return 0;  // Sequence complete, should be matched by now
            }
        }
        return 1;  // Still building sequence
    }
    
    return 0;
}

int vterm_input_feed(char c)
{
    // Timeout: if we're in escape state too long, flush and reset
    if (s_esc_len > 0) {
        if ((xTaskGetTickCount() - s_esc_start) > pdMS_TO_TICKS(20)) {
            flush_input_buffer();
        }
    }
    
    // Start of new escape sequence?
    if (s_esc_len == 0 && c == '\033') {
        s_esc_buf[s_esc_len++] = c;
        s_esc_start = xTaskGetTickCount();
        return 0;  // Buffering
    }
    
    // If we're in an escape sequence, add to buffer
    if (s_esc_len > 0) {
        if (s_esc_len < (int)sizeof(s_esc_buf) - 1) {
            s_esc_buf[s_esc_len++] = c;
        }
        
        // Check for complete hotkey match
        int vt = match_vt_hotkey();
        if (vt >= 0) {
            s_esc_len = 0;
            vterm_switch(vt);
            return 1;  // Hotkey handled
        }
        
        // Could this still become a hotkey?
        if (could_be_hotkey()) {
            return 0;  // Keep buffering
        }
        
        // Not a hotkey, flush as regular input
        flush_input_buffer();
        return 0;
    }
    
    // Normal character, pass through
    vterm_send_input(s_active_vt, c);
    return 0;
}

// ============ Palette API ============

/*
 * Default 16-colour palette (RGB565).
 *
 * The dark half is a little brighter than xterm's, which is what the renderer used before it read
 * this table rather than keeping its own copy. Those values are kept here so the terminal looks the
 * same as it always has; xterm's darker set is legible on a monitor but muddy on this display.
 */
static uint16_t s_palette[16] = {
    0x0000,  // 0: Black
    0xA000,  // 1: Red (dark)
    0x0540,  // 2: Green (dark)
    0xA540,  // 3: Yellow (dark/brown)
    0x0015,  // 4: Blue (dark)
    0xA015,  // 5: Magenta (dark)
    0x0555,  // 6: Cyan (dark)
    0xAD55,  // 7: White (light gray)
    0x52AA,  // 8: Bright Black (dark gray)
    0xF800,  // 9: Bright Red
    0x07E0,  // 10: Bright Green
    0xFFE0,  // 11: Bright Yellow
    0x001F,  // 12: Bright Blue
    0xF81F,  // 13: Bright Magenta
    0x07FF,  // 14: Bright Cyan
    0xFFFF   // 15: Bright White
};

void vterm_set_palette(const uint16_t palette[16])
{
    for (int i = 0; i < 16; i++) {
        s_palette[i] = palette[i];
    }
}

const uint16_t *vterm_get_palette(void)
{
    return s_palette;
}

// ============ Graphics Mode Integration ============

static int s_graphics_mode_active = 0;
static int s_saved_active_vt = -1;

int vterm_enter_graphics_mode(void)
{
    if (s_graphics_mode_active) {
        return 0;  // Already in graphics mode
    }

    // Save current active VT index
    s_saved_active_vt = s_active_vt;

#if VTERM_COUNT > 1
    // Save active VT's IRAM buffer to its PSRAM storage
    vterm_t *active = &s_vterms[s_active_vt];
    xSemaphoreTake(active->mutex, portMAX_DELAY);
    memcpy(active->storage_cells, s_iram_buffer, BUFFER_SIZE_BYTES);
    xSemaphoreGive(active->mutex);
#endif
    // In single VT mode, IRAM buffer stays in place (no storage to save to)

    s_graphics_mode_active = 1;
    return 0;
}

int vterm_exit_graphics_mode(void)
{
    if (!s_graphics_mode_active) {
        return 0;  // Not in graphics mode
    }

#if VTERM_COUNT > 1
    // Restore saved VT state from PSRAM to IRAM
    if (s_saved_active_vt >= 0 && s_saved_active_vt < VTERM_COUNT) {
        vterm_t *vt = &s_vterms[s_saved_active_vt];
        xSemaphoreTake(vt->mutex, portMAX_DELAY);
        memcpy(s_iram_buffer, vt->storage_cells, BUFFER_SIZE_BYTES);
        vt->cells = s_iram_buffer;
        xSemaphoreGive(vt->mutex);

        s_active_vt = s_saved_active_vt;
    }
#endif
    // In single VT mode, IRAM buffer is already in place (never moved)

    s_graphics_mode_active = 0;
    s_saved_active_vt = -1;
    return 0;
}