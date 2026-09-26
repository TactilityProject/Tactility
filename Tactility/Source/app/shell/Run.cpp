#include <Tactility/app/shell/Run.h>

#include "tactility/memory.h"

#include <Tactility/app/shell/Shell.h>
#include <Tactility/app/shell/ShellFs.h>

#include <app/event.h>
#include <app/io.h>
#include <app/manager.h>
#include <app/scheduler.h>
#include <app/start.h>
#include <app/stream.h>

#include <tactility/error.h>
#include <tactility/filesystem/fs.h>
#include <tactility/freertos/task.h>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

int runScript(const char* resolvedPath, int argc, char** argv) {
    struct stat info;
    if (stat(resolvedPath, &info) != 0 || info.st_size < 0) {
        printf("%s: cannot read\n", resolvedPath);
        return 1;
    }

    // +1 to NUL-terminate; file_read_binary() fills a caller-owned buffer, not one of its own.
    const size_t capacity = static_cast<size_t>(info.st_size);
    auto* source = static_cast<char*>(malloc(capacity + 1));
    if (source == nullptr) {
        printf("%s: cannot read\n", resolvedPath);
        return 1;
    }

    size_t size = capacity;
    if (file_read_binary(resolvedPath, reinterpret_cast<uint8_t*>(source), &size) != ERROR_NONE) {
        free(source);
        printf("%s: cannot read\n", resolvedPath);
        return 1;
    }
    source[size] = '\0';

    const int status = Shell::runScriptSource(source, argc, argv);
    free(source);
    return status;
}

namespace {

/**
 * True if `argument` should be made absolute before a loaded binary sees it: a path that exists,
 * or one whose parent directory exists (a file about to be created, e.g. `wget url out.txt`).
 *
 * Strict on purpose: rewriting anything filename-shaped would turn `vistep 3` into
 * `vistep /sdcard/3`. Existence is the only reliable signal, since patterns/numbers/subcommands
 * look just like bare filenames.
 */
bool shouldMakeAbsolute(const char* argument, char* out, size_t outSize) {
    if (argument[0] == '\0' || argument[0] == '-' || argument[0] == '/') {
        return false;
    }

    // Reject anything carrying a character a path would not: ':' from URLs, '*'/'?' from patterns.
    for (const char* p = argument; *p != '\0'; p++) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == ':' || c == '*' || c == '?' || c == ' ' || c == '\t' || c < 0x20) {
            return false;
        }
    }

    char candidate[FILE_MAX_PATH_STRING_LENGTH];
    if (!ShellFs::resolvePath(argument, candidate, sizeof(candidate))) {
        return false;
    }

    if (path_exists(candidate)) {
        snprintf(out, outSize, "%s", candidate);
        return true;
    }

    // Not present: only rewrite if it names a file in a directory that exists and looks like a
    // filename (has '/' or an extension), keeping `grep hello`/`vistep 3` from being treated as paths.
    const bool hasSeparator = strchr(argument, '/') != nullptr;
    const char* dot = strrchr(argument, '.');
    const bool hasExtension = dot != nullptr && dot != argument && dot[1] != '\0';
    if (!hasSeparator && !hasExtension) {
        return false;
    }

    char parent[FILE_MAX_PATH_STRING_LENGTH];
    snprintf(parent, sizeof(parent), "%s", candidate);
    char* lastSlash = strrchr(parent, '/');
    if (lastSlash == nullptr) {
        return false;
    }
    if (lastSlash == parent) {
        parent[1] = '\0';  // the root itself
    } else {
        *lastSlash = '\0';
    }

    if (!directory_exists(parent)) {
        return false;
    }

    snprintf(out, outSize, "%s", candidate);
    return true;
}

// How often the pump loop below checks for a new keystroke while a launched app instance runs.
constexpr uint32_t APP_PUMP_INTERVAL_MS = 10;

// Returned by runApp() when the child never started, as opposed to a real exit status.
constexpr int RUN_APP_START_FAILED = -1;

bool lastByteWasNewline = true;

/** Copies what a child wrote to one of its output streams to `target`, which may be redirected. */
void drainOutput(AppStream& stream, FILE* target, uint8_t* buffer, size_t size) {
    size_t n;
    while ((n = app_stream_read(&stream, buffer, size)) > 0) {
        fwrite(buffer, 1, n, target);   // byte-counted: output may contain NUL bytes
        lastByteWasNewline = buffer[n - 1] == '\n';
    }
}

/**
 * Runs an app instance to completion, piping its stdio to/from this task's own, like a shell
 * piping a child process. `context` must already have its location and arguments set.
 *
 * Shared by runElf() and runFromMemory() so a registered command gets the same task/fd isolation
 * as a loaded ELF binary, instead of running inline on the shell's own task.
 *
 * @return the child's exit status, or RUN_APP_START_FAILED if it never started
 */
int runApp(AppStartContext& context) {
    constexpr size_t STDIN_BUFFER_SIZE = 256;
    constexpr size_t STDOUT_BUFFER_SIZE = 1024;
    constexpr size_t STDERR_BUFFER_SIZE = 256;
    constexpr MemoryPolicy policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
    auto* stdinBuffer = static_cast<uint8_t*>(memory_alloc_with_policy(STDIN_BUFFER_SIZE, &policy));
    auto* stdoutBuffer = static_cast<uint8_t*>(memory_alloc_with_policy(STDOUT_BUFFER_SIZE, &policy));
    auto* stderrBuffer = static_cast<uint8_t*>(memory_alloc_with_policy(STDERR_BUFFER_SIZE, &policy));
    if (stdinBuffer == nullptr || stdoutBuffer == nullptr || stderrBuffer == nullptr) {
        memory_free(stdinBuffer);
        memory_free(stdoutBuffer);
        memory_free(stderrBuffer);
        return RUN_APP_START_FAILED;
    }

    AppStream stdinStream {};
    AppStream stdoutStream {};
    // Kept apart from stdout, so `$(...)` captures only a child's stdout, like any shell
    AppStream stderrStream {};

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);

    // Best-effort: lets a child querying APP_IOCTL_GET_WINDOW_SIZE inherit this task's own,
    // rather than see unset since it has no real tty of its own. Read before the child exists
    // and passed into its binding, not set on the stream after app_start_with_context() returns:
    // a fast-finishing child's whole main() can already have run by the time that call returns,
    // so anything set afterwards can be too late for it to ever observe.
    AppWindowSize windowSize {};
    app_io_ioctl(STDOUT_FILENO, APP_IOCTL_GET_WINDOW_SIZE, &windowSize);

    AppStreamBinding bindings[] = {
        { STDIN_FILENO, &stdinStream, stdinBuffer, STDIN_BUFFER_SIZE, &eventGroup, {}, -1 },
        { STDOUT_FILENO, &stdoutStream, stdoutBuffer, STDOUT_BUFFER_SIZE, &eventGroup, windowSize, -1 },
        { STDERR_FILENO, &stderrStream, stderrBuffer, STDERR_BUFFER_SIZE, &eventGroup, windowSize, -1 },
    };

    AppEventSubscription eventSub {};
    app_event_subscribe(&eventSub, &eventGroup);

    app_start_context_set_streams(&context, bindings, sizeof(bindings) / sizeof(bindings[0]));
    app_start_context_set_parent(&context, app_scheduler_current_app_id());

    AppInstanceId childId = 0;
    error_t result = app_start_with_context(&context, &childId);
    if (result != ERROR_NONE) {
        app_event_unsubscribe(&eventSub);
        task_event_group_destruct(&eventGroup);
        memory_free(stdinBuffer);
        memory_free(stdoutBuffer);
        memory_free(stderrBuffer);
        return RUN_APP_START_FAILED;
    }

    // Polls stdin rather than blocking: a non-interactive/early-exiting child stops reading it, so
    // a plain read() would block forever with no way to notice the child exited.
    int32_t childResult = 2; // AppResultEventData's "Error"; used only if the loop below exits
                              // without observing the matching APP_EVENT_RESULT
    bool childDone = false;
    bool ownStdinClosed = false;
    // TODO: Consider PSRAM
    uint8_t drain[256];

    while (!childDone) {
        bool waited = false;
        if (!ownStdinClosed) {
            const error_t awaited = app_io_await(STDIN_FILENO, APP_FILE_WAIT_READABLE, pdMS_TO_TICKS(APP_PUMP_INTERVAL_MS));
            waited = true;
            if (awaited == ERROR_NONE) {
                char ch = 0;
                const ssize_t n = app_io_read(STDIN_FILENO, &ch, 1);
                if (n == 1) {
                    app_stream_write(&stdinStream, &ch, 1);
                } else {
                    // Our own stdin hung up (touch-to-exit): propagate to the child the same way.
                    app_stream_close(&stdinStream);
                    ownStdinClosed = true;
                }
            }
        }
        if (!waited) {
            vTaskDelay(pdMS_TO_TICKS(APP_PUMP_INTERVAL_MS));
        }

        drainOutput(stdoutStream, stdout, drain, sizeof(drain));
        drainOutput(stderrStream, stderr, drain, sizeof(drain));

        AppEvent event {};
        while (app_event_poll(&eventSub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_RESULT && event.result.launch_id == childId) {
                childResult = event.result.result;
                childDone = true;
            }
        }
    }

    // The child may have exited right after its last write, before the loop above's last read.
    drainOutput(stdoutStream, stdout, drain, sizeof(drain));
    drainOutput(stderrStream, stderr, drain, sizeof(drain));

    // Must run before app_manager_stop() reaps the child: only app_stream_unsubscribe() guarantees
    // the fd binding is gone and no AppFileOps call is still in flight, which is what makes these
    // stack-local AppStreams safe to let go out of scope below.
    app_stream_unsubscribe(&stdinStream);
    app_stream_unsubscribe(&stdoutStream);
    app_stream_unsubscribe(&stderrStream);

    app_manager_stop(childId);

    app_event_unsubscribe(&eventSub);
    task_event_group_destruct(&eventGroup);

    memory_free(stdinBuffer);
    memory_free(stdoutBuffer);
    memory_free(stderrBuffer);

    return childResult;
}

} // namespace

void endLineIfNeeded() {
    if (!lastByteWasNewline) {
        printf("\n");
        lastByteWasNewline = true;
    }
}

int runElf(const char* resolvedPath, int argc, char** argv) {
    // Relative-looking arguments are made absolute before the binary sees them: ESP-IDF has no
    // per-process cwd for fopen() to resolve against (no chdir() at all), and the shell's own cwd
    // lives in ShellFs, meaning nothing to libc. shouldMakeAbsolute() decides which arguments qualify.
    char* rewritten[32];
    constexpr int STORAGE_COUNT = 8;
    // On the heap: the loader runs on this task's stack right after, and needs the room
    constexpr MemoryPolicy policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
    auto* storage = static_cast<char*>(memory_alloc_with_policy(STORAGE_COUNT * FILE_MAX_PATH_STRING_LENGTH, &policy));
    if (storage == nullptr) {
        printf("%s: out of memory\n", argv[0]);
        return 126;
    }
    int stored = 0;

    const int passedArgc = (argc < static_cast<int>(sizeof(rewritten) / sizeof(rewritten[0])))
        ? argc
        : static_cast<int>(sizeof(rewritten) / sizeof(rewritten[0]));

    for (int i = 0; i < passedArgc; i++) {
        rewritten[i] = argv[i];

        // argv[0] is the binary itself, already resolved by the caller.
        if (i == 0 || stored >= STORAGE_COUNT) {
            continue;
        }

        char* slot = storage + stored * FILE_MAX_PATH_STRING_LENGTH;
        if (shouldMakeAbsolute(argv[i], slot, FILE_MAX_PATH_STRING_LENGTH)) {
            rewritten[i] = slot;
            stored++;
        }
    }

    // argv[0] is given as the resolved path too, so a binary that reopens itself still works.
    rewritten[0] = const_cast<char*>(resolvedPath);

    // $PWD lets a binary undo the rewriting above when it needs the name the user actually typed
    // (e.g. tar stripping /sdcard/test.sh back to test.sh before archiving it). Leaks into the
    // shell's own environment harmlessly, since the shell reads its cwd from ShellFs, not getenv.
    setenv("PWD", ShellFs::cwd(), 1);

    // Output written by the binary should appear as it is produced rather than sitting in a buffer
    // until the binary happens to exit.
    fflush(stdout);

    AppLocation location { APP_LOCATION_PATH, const_cast<char*>(resolvedPath) };
    AppStartContext context = app_start_context_for_location(location);
    app_start_context_set_arguments_ext(&context, passedArgc, rewritten);

    const int result = runApp(context);
    memory_free(storage);

    int exitCode;
    if (result == RUN_APP_START_FAILED) {
        // Covers both "not a valid/runnable ELF" and "references a symbol the firmware does not
        // export"; the loader logs specifics to the serial console.
        printf("%s: failed to load (see serial log for details)\n", argv[0]);
        exitCode = 126;
    } else {
        exitCode = result;
    }

    fflush(stdout);
    return exitCode;
}

int runFromMemory(const char* id, int argc, char** argv) {
    AppStartContext context;
    if (app_start_context_from_id(id, &context) != ERROR_NONE) {
        printf("%s: not found\n", id);
        return 127;
    }
    app_start_context_set_arguments_ext(&context, argc, argv);

    fflush(stdout);

    const int result = runApp(context);

    int exitCode;
    if (result == RUN_APP_START_FAILED) {
        printf("%s: failed to start (see serial log for details)\n", id);
        exitCode = 126;
    } else {
        exitCode = result;
    }

    fflush(stdout);
    return exitCode;
}
