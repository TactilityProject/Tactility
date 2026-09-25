#include <Tactility/app/terminal/KeyboardInput.h>
#include <Tactility/app/terminal/Scrollback.h>
#include <Tactility/app/terminal/Shell.h>
#include <Tactility/app/terminal/Terminal.h>
#include <Tactility/app/terminal/TerminalRenderer.h>
#include <Tactility/app/terminal/TerminalRendererGeneric.h>
#include <Tactility/app/terminal/TerminalRendererPpa.h>
#include <Tactility/app/terminal/TouchInput.h>

#include <tactility/device.h>
#include <tactility/drivers/keyboard.h>

#include <tactility/log.h>

#include <tactility/freertos/freertos.h>
#include <tactility/freertos/semphr.h>
#include <tactility/freertos/task.h>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

extern "C" {
#include <Tactility/app/terminal/vterm/vterm.h>
}

constexpr auto* TAG = "terminal";

// Redraw cadence. A text grid only changes when something is written, so this is a polling
// interval rather than a frame rate.
constexpr uint32_t RENDER_INTERVAL_MS = 33;

// The I/O task polls the keyboards, paints the screen, and watches for the touch-to-exit gesture.
// It runs a step above this app's own task: input, drawing and the exit gesture must keep working
// while that task is blocked draining a shell that is itself blocked running something, which is
// the entire reason this is a separate task.
constexpr uint32_t IO_TASK_STACK = 8 * 1024;
constexpr UBaseType_t IO_TASK_PRIORITY = 6;

// Lines of scrollback kept, and how far one Ctrl+Up/Down moves the view. 500 lines of an 80-column
// terminal is about 80KB, which is nothing in PSRAM and covers any realistic burst of output.
constexpr int SCROLLBACK_LINES = 500;
constexpr int SCROLL_STEP_LINES = 5;

namespace {

/** Set once the shell should wind down (touch-to-exit; see ioTask()). Read by runTerminal()'s own
 * pump loop, which closes the shell app's stdin to unstick it (see that loop's own comment). */
volatile bool stopRequested = false;

/**
 * Translates a kernel keyboard event into the byte a terminal expects.
 * Returns 0 for keys with no terminal representation.
 */
char translateKey(const KeyboardKeyData& data) {
    // Ctrl chords first, so they win over the plain-letter reading of the same key code.
    if (data.ctrl && data.key >= 'a' && data.key <= 'z') {
        return static_cast<char>(data.key & 0x1F);
    }
    if (data.ctrl && data.key >= 'A' && data.key <= 'Z') {
        return static_cast<char>(data.key & 0x1F);
    }

    switch (data.key) {
        case CODEPOINT_ENTER: return '\r';
        case CODEPOINT_BACKSPACE: return 0x7F; // DEL, which is what linenoise expects for backspace
        case CODEPOINT_ESCAPE: return 0x1B;
        case CODEPOINT_DELETE: return 0x7F;
        default: break;
    }

    if (data.key == CODEPOINT_TAB && !data.ctrl) {
        return '\t';
    }

    if (data.key >= 0x20 && data.key < 0x7F) {
        return static_cast<char>(data.key);
    }

    return 0;
}

/**
 * Feeds arrow keys to the terminal as ANSI escape sequences,
 * which is how linenoise recognises history navigation and cursor movement.
 */
void feedArrowKey(uint32_t key) {
    const char* sequence = nullptr;
    switch (key) {
        case CODEPOINT_ARROW_UP: sequence = "\x1B[A"; break;
        case CODEPOINT_ARROW_DOWN: sequence = "\x1B[B"; break;
        case CODEPOINT_ARROW_RIGHT: sequence = "\x1B[C"; break;
        case CODEPOINT_ARROW_LEFT: sequence = "\x1B[D"; break;
        default: return;
    }
    for (const char* c = sequence; *c != '\0'; c++) {
        vterm_send_input(vterm_get_active(), *c);
    }
}

/**
 * Handles one key from a keyboard.
 * @return true if the scrollback view moved
 */
bool handleKey(unsigned int key, bool ctrl, bool alt) {
    (void)alt;

    if (ctrl) {
        if (key == CODEPOINT_ARROW_UP) {
            return Scrollback::scroll(SCROLL_STEP_LINES);
        }
        if (key == CODEPOINT_ARROW_DOWN) {
            return Scrollback::scroll(-SCROLL_STEP_LINES);
        }
    }

    // Any other key returns to the live screen: someone typing wants to see the prompt they are
    // typing at rather than whatever history was being reviewed.
    const bool viewChanged = Scrollback::reset();

    // Plain arrows become escape sequences; everything else maps to a single byte.
    if (!ctrl && (key == CODEPOINT_ARROW_UP || key == CODEPOINT_ARROW_DOWN ||
                  key == CODEPOINT_ARROW_LEFT || key == CODEPOINT_ARROW_RIGHT)) {
        feedArrowKey(key);
        return viewChanged;
    }

    KeyboardKeyData data {};
    data.key = key;
    data.pressed = true;
    data.ctrl = ctrl;
    data.alt = alt;

    const char c = translateKey(data);
    if (c != 0) {
        vterm_send_input(vterm_get_active(), c);
    }
    return viewChanged;
}

/**
 * Parameters for the I/O task. Lives in runTerminal()'s frame, which outlives the task itself:
 * runTerminal() does not return until the I/O task has acknowledged the stop request.
 */
struct IoTaskParams {
    KeyboardInput* keyboards;
    TouchInput* touch;
    TerminalRenderer* renderer;
    SemaphoreHandle_t doneSem;
};

/** Keyboard/touch input and display rendering. */
void ioTask(void* arg) {
    auto* params = static_cast<IoTaskParams*>(arg);

    while (!stopRequested) {
        // A scroll replaces every row at once, so the renderer is told to repaint rather than rely
        // on its per-cell comparison.
        const bool viewMoved = params->keyboards->pump(handleKey);
        params->renderer->render(viewMoved);

        if (params->touch->touched(!params->keyboards->empty())) {
            LOG_I(TAG, "Touch detected - stopping");
            stopRequested = true;
        }

        vTaskDelay(pdMS_TO_TICKS(RENDER_INTERVAL_MS));
    }

    xSemaphoreGive(params->doneSem);
    vTaskDelete(nullptr);
}

} // namespace

void runTerminal(Device* display) {
    stopRequested = false;

    KeyboardInput keyboards;
    TouchInput touch;

    if (vterm_init() != ERROR_NONE) {
        LOG_E(TAG, "vterm_init failed");
        return;
    }

    TerminalRendererPpa ppaRenderer;
    TerminalRendererGeneric genericRenderer;
    TerminalRenderer& renderer = TerminalRendererPpa::isSupported()
        ? static_cast<TerminalRenderer&>(ppaRenderer)
        : static_cast<TerminalRenderer&>(genericRenderer);
    if (!renderer.begin(display)) {
        LOG_E(TAG, "Renderer failed to start");
        vterm_deinit();
        return;
    }

    // Tell vterm the real drawable size, which may be smaller than its compiled-in grid.
    vterm_set_size_override(renderer.rows(), renderer.columns());

    // History of lines that scroll off the top. vterm discards them, so the callback below catches
    // each one while it is still readable. Failure is not fatal; the terminal simply has no scrollback.
    if (Scrollback::begin(renderer.columns(), SCROLLBACK_LINES)) {
        vterm_set_scroll_callback(scrollback_capture_top_line);
    }

    renderer.render(true);

    // Input, drawing and the touch-to-exit gesture run on their own task, so that all three keep
    // working while this one is blocked draining the shell app. See ioTask().
    IoTaskParams ioParams {
        .keyboards = &keyboards,
        .touch = &touch,
        .renderer = &renderer,
        .doneSem = xSemaphoreCreateBinary(),
    };
    TaskHandle_t ioHandle = nullptr;
    if (ioParams.doneSem != nullptr) {
        if (xTaskCreate(ioTask, "terminal-io", IO_TASK_STACK, &ioParams,
                        IO_TASK_PRIORITY, &ioHandle) != pdPASS) {
            ioHandle = nullptr;
        }
    }
    if (ioHandle == nullptr) {
        LOG_E(TAG, "I/O task failed to start - the terminal cannot run");
    } else {
#ifdef ESP_PLATFORM
        esp_log_level_set("ELF", ESP_LOG_WARN);
#endif
        runShell(renderer.columns(), renderer.rows(), &stopRequested);
#ifdef ESP_PLATFORM
        esp_log_level_set("ELF", ESP_LOG_INFO);
#endif
    }

    if (ioHandle != nullptr) {
        xSemaphoreTake(ioParams.doneSem, portMAX_DELAY);
    }
    if (ioParams.doneSem != nullptr) {
        vSemaphoreDelete(ioParams.doneSem);
    }

    vterm_set_scroll_callback(nullptr);
    Scrollback::end();

    renderer.end();

    vterm_deinit();

    LOG_I(TAG, "Terminal stopped");
}
