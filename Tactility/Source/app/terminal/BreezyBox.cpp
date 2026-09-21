#include <Tactility/app/terminal/BreezyBox.h>
#include <Tactility/app/terminal/Scrollback.h>
#include <Tactility/app/terminal/TerminalRenderer.h>
#include <Tactility/app/terminal/TerminalRendererGeneric.h>
#include <Tactility/app/terminal/TerminalRendererPpa.h>

#include <app/event.h>
#include <app/io.h>
#include <app/manager.h>
#include <app/scheduler.h>
#include <app/start.h>
#include <app/stream.h>

#include <tactility/device.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/drivers/pointer.h>

#include <tactility/log.h>

#include <tactility/freertos/freertos.h>
#include <tactility/freertos/semphr.h>
#include <tactility/freertos/task.h>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

#include <lvgl.h> // for the LV_KEY_* constants the kernel keyboard drivers emit

#include <cstdio>

extern "C" {
#include <Tactility/app/terminal/vterm/vterm.h>
}

constexpr auto* TAG = "BreezyBox";

// Redraw cadence. A text grid only changes when something is written, so this is a polling
// interval rather than a frame rate.
constexpr uint32_t RENDER_INTERVAL_MS = 33;

// The I/O task polls the keyboards, paints the screen, and watches for the touch-to-exit gesture.
// It runs a step above this app's own task: input, drawing and the exit gesture must keep working
// while that task is blocked draining a shell that is itself blocked running something, which is
// the entire reason this is a separate task.
constexpr uint32_t IO_TASK_STACK = 8 * 1024;
constexpr UBaseType_t IO_TASK_PRIORITY = 6;

// How often the pump loop below checks for new keyboard input while draining the shell app.
constexpr uint32_t SHELL_PUMP_INTERVAL_MS = 50;

// Lines of scrollback kept, and how far one Ctrl+Up/Down moves the view. 500 lines of an 80-column
// terminal is about 80KB, which is nothing in PSRAM and covers any realistic burst of output.
constexpr int SCROLLBACK_LINES = 500;
constexpr int SCROLL_STEP_LINES = 5;

namespace {

/** Set once the shell should wind down (touch-to-exit; see ioTask()). Read by runTerminal()'s own
 * pump loop, which closes the shell app's stdin to unstick it - see that loop's own comment. */
volatile bool stopRequested = false;

/**
 * Translates a kernel keyboard event into the byte a terminal expects.
 *
 * The `ctrl` flag is what makes this possible: the C0 control codes a shell needs (Ctrl+C = 0x03,
 * Ctrl+D = 0x04) occupy the same numeric range as the LVGL key constants the driver emits in
 * `key` (LV_KEY_END = 3, LV_KEY_PREV = 11), so the two can only be told apart out-of-band.
 *
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

    // LV_KEY_NEXT is 0x09, which is also Tab. With ctrl clear it can only be a real Tab press,
    // since the driver emits NEXT only for Ctrl+arrow.
    if (data.key == CODEPOINT_TAB && !data.ctrl) {
        return '\t';
    }

    if (data.key >= 0x20 && data.key < 0x7F) {
        return static_cast<char>(data.key);
    }

    return 0;
}

/**
 * Feeds arrow keys to the terminal as ANSI escape sequences, which is how linenoise recognises
 * history navigation and cursor movement.
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
 * Reads pending keys.
 * @return true if the scrollback view moved, so the caller redraws the whole screen
 */
/**
 * Handles one key from either keyboard.
 *
 * The built-in driver and the USB HID driver both report LVGL key codes with the modifiers
 * separately, so a USB keyboard behaves identically to the addon - including Ctrl chords, which
 * neither driver can express in the key code itself.
 *
 * @return true if the scrollback view moved
 */
bool handleKey(unsigned int key, bool ctrl, bool alt) {
    (void)alt;

    // Ctrl+Up/Down scroll the history.
    //
    // The two keyboards report this differently: the built-in driver remaps Ctrl+arrow to
    // PREV/NEXT (its LVGL focus-navigation convention), while the USB HID driver leaves the arrow
    // code alone and only sets the ctrl flag. Both forms are accepted so the key works the same on
    // either keyboard. The ctrl flag is what separates them from a plain Tab or arrow press.
    if (ctrl) {
        if (key == CODEPOINT_ARROW_UP || key == CODEPOINT_ARROW_UP) {
            return Scrollback::scroll(SCROLL_STEP_LINES);
        }
        if (key == CODEPOINT_ARROW_DOWN || key == CODEPOINT_ARROW_DOWN) {
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
 * Reads pending keys from every keyboard device found at startup (built-in, USB, or both - see
 * main.cpp's device_for_each_of_type(&KEYBOARD_TYPE, ...) call). Both feed the same handler
 * through the same kernel keyboard.h interface, so either can be used at any time - or both.
 * @return true if the scrollback view moved, so the caller redraws the whole screen
 */
bool pumpKeyboard(Device* const* keyboards, int keyboardCount) {
    bool viewChanged = false;

    KeyboardKeyData data;
    for (int i = 0; i < keyboardCount; i++) {
        Device* keyboard = keyboards[i];
        while (true) {
            if (keyboard_read_key(keyboard, &data) != ERROR_NONE) {
                break;
            }
            if (!data.pressed) {
                break;
            }
            if (handleKey(data.key, data.ctrl, data.alt)) {
                viewChanged = true;
            }
            if (!data.continue_reading) {
                break;
            }
        }
    }

    return viewChanged;
}

bool isTouched(Device* touch) {
    if (pointer_read_data(touch, 0) != ERROR_NONE) {
        return false;
    }
    uint16_t x, y, strength;
    uint8_t pointCount = 0;
    return pointer_get_touched_points(touch, &x, &y, &strength, &pointCount, 1);
}

/**
 * Parameters for the I/O task. Lives in runTerminal()'s frame, which outlives the task itself:
 * runTerminal() does not return until the I/O task has acknowledged the stop request.
 */
struct IoTaskParams {
    Device* const* keyboards;
    int keyboardCount;
    TerminalRenderer* renderer;
    /** Optional; see isTouched(). NULL disables the touch-to-exit gesture. */
    Device* touch;
    SemaphoreHandle_t doneSem;
};

/**
 * Polls the keyboards into vterm's input queue, paints the screen, and watches for the
 * touch-to-exit gesture.
 *
 * All three belong on a task of their own: runTerminal()'s own task is blocked pumping the shell
 * app's stdio (see runShell()), which may itself be blocked waiting for an interactive program's
 * own read() - vi sits waiting for a keystroke that would otherwise never arrive, and the same
 * would go for touch-to-exit. Non-interactive tools (grep, diff, tar) never depended on this,
 * because they read no input at all. The cursor position report is equally independent of it:
 * vterm answers ESC[6n from inside its own escape parser, synchronously during the write.
 *
 * Rendering has to move here as well, and not merely be called from both places - the renderer
 * keeps a shadow buffer and cursor state that two tasks would corrupt. Running it here means a
 * program's output appears while it is still running rather than only once it exits, which is what
 * an interactive program needs in any case.
 */
void ioTask(void* arg) {
    auto* params = static_cast<IoTaskParams*>(arg);

    while (!stopRequested) {
        // A scroll replaces every row at once, so the renderer is told to repaint rather than rely
        // on its per-cell comparison.
        const bool viewMoved = pumpKeyboard(params->keyboards, params->keyboardCount);
        params->renderer->render(viewMoved);

        if (params->touch != nullptr && isTouched(params->touch)) {
            LOG_I(TAG, "Touch detected - stopping");
            stopRequested = true;
        }

        vTaskDelay(pdMS_TO_TICKS(RENDER_INTERVAL_MS));
    }

    xSemaphoreGive(params->doneSem);
    vTaskDelete(nullptr);
}

/**
 * Launches the shell app and relays its stdio to/from vterm until it exits on its own or
 * stopRequested asks it to (touch-to-exit) - the same relationship a real terminal emulator has
 * to the shell it runs over a pty. Sets stopRequested before returning either way, so ioTask's own
 * loop (still running independently) winds down too.
 */
void runShell(int columns) {
    static uint8_t stdinBuffer[256];
    static uint8_t stdoutBuffer[1024];
    static uint8_t stderrBuffer[512];
    AppStream stdinStream {};
    AppStream stdoutStream {};
    AppStream stderrStream {};

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);

    AppStreamBinding bindings[] = {
        { STDIN_FILENO, &stdinStream, stdinBuffer, sizeof(stdinBuffer), &eventGroup },
        { STDOUT_FILENO, &stdoutStream, stdoutBuffer, sizeof(stdoutBuffer), &eventGroup },
        { STDERR_FILENO, &stderrStream, stderrBuffer, sizeof(stderrBuffer), &eventGroup },
    };

    AppEventSubscription eventSub {};
    app_event_subscribe(&eventSub, &eventGroup);

    // There is no ioctl(TIOCGWINSZ) here, so the shell app learns the real width this way instead
    // (see its own LineEditor::setTerminalColumns()).
    char columnsArg[8];
    snprintf(columnsArg, sizeof(columnsArg), "%d", columns);
    const char* argv[] = { "shell", columnsArg };

    AppInstanceId shellId = 0;
    error_t result = app_start_for_result_with_streams(
        "shell", 2, argv, bindings, sizeof(bindings) / sizeof(bindings[0]),
        app_scheduler_current_app_id(), &shellId);
    if (result != ERROR_NONE) {
        LOG_E(TAG, "Failed to start shell app");
        app_event_unsubscribe(&eventSub);
        task_event_group_destruct(&eventGroup);
        stopRequested = true;
        return;
    }

    LOG_I(TAG, "Terminal started - touch to exit");

    bool shellStdinClosed = false;
    bool shellDone = false;
    uint8_t drain[256];

    while (!shellDone) {
        const int c = vterm_getchar(vterm_get_active(), pdMS_TO_TICKS(SHELL_PUMP_INTERVAL_MS));
        if (c >= 0 && !shellStdinClosed) {
            const char ch = static_cast<char>(c);
            app_stream_write(&stdinStream, &ch, 1);
        }

        size_t n;
        while ((n = app_stream_read(&stdoutStream, drain, sizeof(drain))) > 0) {
            vterm_write_translated(reinterpret_cast<const char*>(drain), n);
        }
        while ((n = app_stream_read(&stderrStream, drain, sizeof(drain))) > 0) {
            vterm_write_translated(reinterpret_cast<const char*>(drain), n);
        }

        // The shell app never sees stopRequested directly - its stdin is closed instead, which
        // unsticks a read() blocked waiting on it the same way any closed pipe does.
        if (stopRequested && !shellStdinClosed) {
            app_stream_close(&stdinStream);
            shellStdinClosed = true;
        }

        AppEvent event {};
        while (app_event_poll(&eventSub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_RESULT && event.result.launch_id == shellId) {
                shellDone = true;
            }
        }
    }

    // The shell app may have written its last bytes and exited before the loop above's last read
    // saw them.
    size_t n;
    while ((n = app_stream_read(&stdoutStream, drain, sizeof(drain))) > 0) {
        vterm_write_translated(reinterpret_cast<const char*>(drain), n);
    }
    while ((n = app_stream_read(&stderrStream, drain, sizeof(drain))) > 0) {
        vterm_write_translated(reinterpret_cast<const char*>(drain), n);
    }

    // Only app_stream_unsubscribe() guarantees the fd-table binding is gone and no AppFileOps call
    // is still in flight, which is what makes these stack-local AppStreams safe to let go out of
    // scope below. Must happen before app_manager_stop() reaps the shell app, in case that races
    // app_fd_table_teardown()'s own close() of these same fds.
    app_stream_unsubscribe(&stdinStream);
    app_stream_unsubscribe(&stdoutStream);
    app_stream_unsubscribe(&stderrStream);

    app_manager_stop(shellId);

    app_event_unsubscribe(&eventSub);
    task_event_group_destruct(&eventGroup);

    stopRequested = true;
}

} // namespace

/**
 * Runs the terminal to completion on the calling task. Input, drawing and the touch-to-exit
 * gesture run on a second task (see ioTask()) so they keep working while this one is blocked
 * draining the shell app, which may itself be blocked running something.
 */
void runTerminal(Device* display, Device* const* keyboards, int keyboardCount, Device* touch) {
    stopRequested = false;

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
        return;
    }

    // Tell vterm the real drawable size, which may be smaller than its compiled-in grid.
    vterm_set_size_override(renderer.rows(), renderer.columns());

    // History of lines that scroll off the top. vterm discards them, so the callback below catches
    // each one while it is still readable. Failure is not fatal - the terminal simply has no
    // scrollback.
    if (Scrollback::begin(renderer.columns(), SCROLLBACK_LINES)) {
        vterm_set_scroll_callback(scrollback_capture_top_line);
    }

    renderer.render(true);

    // Input, drawing and the touch-to-exit gesture run on their own task, so that all three keep
    // working while this one is blocked draining the shell app. See ioTask().
    IoTaskParams ioParams {
        .keyboards = keyboards,
        .keyboardCount = keyboardCount,
        .renderer = &renderer,
        .touch = touch,
        .doneSem = xSemaphoreCreateBinary()
    };
    TaskHandle_t ioHandle = nullptr;
    if (ioParams.doneSem != nullptr) {
        if (xTaskCreate(ioTask, "breezyio", IO_TASK_STACK, &ioParams,
                        IO_TASK_PRIORITY, &ioHandle) != pdPASS) {
            ioHandle = nullptr;
        }
    }
    if (ioHandle == nullptr) {
        LOG_E(TAG, "I/O task failed to start - the terminal cannot run");
    } else {
        // Suppressed for the duration: the loader reports its version, segment padding and entry
        // point on every launch of a program the shell app runs, which is noise in front of a
        // user - its own output reaches this screen the same way anything else it prints does, by
        // being piped back through the shell app above. Warnings and errors still come through,
        // since those explain a failed load. Per-tag log level control is an ESP-IDF-only concept.
#ifdef ESP_PLATFORM
        esp_log_level_set("ELF", ESP_LOG_WARN);
#endif
        runShell(renderer.columns());
#ifdef ESP_PLATFORM
        esp_log_level_set("ELF", ESP_LOG_INFO);
#endif
    }

    // The I/O task reads `ioParams`, which lives in this frame, so wait for it to actually finish
    // before returning.
    if (ioHandle != nullptr) {
        xSemaphoreTake(ioParams.doneSem, pdMS_TO_TICKS(1000));
    }
    if (ioParams.doneSem != nullptr) {
        vSemaphoreDelete(ioParams.doneSem);
    }

    vterm_set_scroll_callback(nullptr);
    Scrollback::end();

    renderer.end();

    LOG_I(TAG, "Terminal stopped");
}
