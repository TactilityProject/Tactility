#include <Tactility/app/terminal/Shell.h>

#include <app/event.h>
#include <app/io.h>
#include <app/manager.h>
#include <app/scheduler.h>
#include <app/start.h>
#include <app/stream.h>

#include <tactility/log.h>

#include <tactility/freertos/freertos.h>
#include <tactility/freertos/task.h>

#include <cstdio>

extern "C" {
#include <Tactility/app/terminal/vterm/vterm.h>
}

namespace {

constexpr auto* TAG = "terminal";

// How often the pump loop below checks for new keyboard input while draining the shell app.
constexpr uint32_t SHELL_PUMP_INTERVAL_MS = 50;

} // namespace

void runShell(int columns, int rows, volatile bool* stopRequested) {
    static uint8_t stdinBuffer[256];
    static uint8_t stdoutBuffer[1024];
    AppStream stdinStream {};
    AppStream stdoutStream {};

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);

    // Queried by the shell app via app_io_ioctl(STDOUT_FILENO, APP_IOCTL_GET_WINDOW_SIZE, ...).
    // Passed into the binding rather than set on the stream after app_start_with_context()
    // returns: that would race the child's own first read of it (see AppStreamBinding::window_size).
    const AppWindowSize windowSize { static_cast<uint16_t>(columns), static_cast<uint16_t>(rows) };

    AppStreamBinding bindings[] = {
        { STDIN_FILENO, &stdinStream, stdinBuffer, sizeof(stdinBuffer), &eventGroup, {} },
        { STDOUT_FILENO, &stdoutStream, stdoutBuffer, sizeof(stdoutBuffer), &eventGroup, windowSize, STDERR_FILENO },
    };

    AppEventSubscription eventSub {};
    app_event_subscribe(&eventSub, &eventGroup);

    AppInstanceId shellId = 0;
    AppStartContext context;
    error_t result = app_start_context_from_id("shell", &context);
    if (result == ERROR_NONE) {
        app_start_context_set_streams(&context, bindings, sizeof(bindings) / sizeof(bindings[0]));
        app_start_context_set_parent(&context, app_scheduler_current_app_id());
        result = app_start_with_context(&context, &shellId);
    }
    if (result != ERROR_NONE) {
        LOG_E(TAG, "Failed to start shell app");
        app_event_unsubscribe(&eventSub);
        task_event_group_destruct(&eventGroup);
        *stopRequested = true;
        return;
    }

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

        // The shell app never sees stopRequested directly: its stdin is closed instead, which
        // unsticks a read() blocked waiting on it the same way any closed pipe does.
        if (*stopRequested && !shellStdinClosed) {
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

    // The shell app may have written its last bytes and exited before the loop above's last read saw them.
    size_t n;
    while ((n = app_stream_read(&stdoutStream, drain, sizeof(drain))) > 0) {
        vterm_write_translated(reinterpret_cast<const char*>(drain), n);
    }

    // Only app_stream_unsubscribe() guarantees the fd-table binding is gone and no AppFileOps call
    // is still in flight, which is what makes these stack-local AppStreams safe to let go out of
    // scope below. Must happen before app_manager_stop() reaps the shell app, in case that races
    // app_fd_table_teardown()'s own close() of these same fds. The stderr alias fd is closed by
    // that teardown too.
    app_stream_unsubscribe(&stdinStream);
    app_stream_unsubscribe(&stdoutStream);

    app_manager_stop(shellId);

    app_event_unsubscribe(&eventSub);
    task_event_group_destruct(&eventGroup);

    *stopRequested = true;
}
