#include <Tactility/app/shell/commands/CommandSupport.h>

#include <Tactility/app/shell/Shell.h>
#include <Tactility/app/shell/ShellFs.h>

#include <app/event.h>
#include <app/execute.h>
#include <app/io.h>
#include <app/manager.h>
#include <app/scheduler.h>
#include <app/stream.h>

#include <tactility/error.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/freertos/task.h>

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

bool resolveArg(const char* command, const char* path, char* out, size_t outSize) {
    if (!ShellFs::resolvePath(path, out, outSize)) {
        printf("%s: %s: path too long\n", command, path);
        return false;
    }
    return true;
}

int reportResult(const char* command, const char* subject, ShellFs::Result result) {
    if (result == ShellFs::Result::Ok) {
        return 0;
    }
    printf("%s: %s: %s\n", command, subject, ShellFs::describe(result));
    return 1;
}

const char* const COLOUR_RESET = "\x1B[0m";
const char* const COLOUR_DIR = "\x1B[94m";      // bright blue
const char* const COLOUR_EXEC = "\x1B[92m";     // bright green
const char* const COLOUR_SIZE = "\x1B[90m";     // grey, so it recedes behind the names
const char* const COLOUR_ERROR = "\x1B[91m";    // bright red

bool looksExecutable(const char* name) {
    const char* dot = strrchr(name, '.');
    if (dot == nullptr) {
        return false;
    }
    return strcmp(dot, ".elf") == 0 || strcmp(dot, ".sh") == 0;
}

bool printMount(FileSystem* fs, void*) {
    char path[ShellFs::MAX_PATH];
    if (file_system_get_path(fs, path, sizeof(path)) == ERROR_NONE) {
        printf("%s%s%s\n", COLOUR_DIR, path, COLOUR_RESET);
    }
    return true;
}

void printEntry(const ShellFs::Entry& entry, void*) {
    if (entry.isDirectory) {
        printf("%s%s/%s\n", COLOUR_DIR, entry.name, COLOUR_RESET);
        return;
    }

    const char* nameColour = looksExecutable(entry.name) ? COLOUR_EXEC : "";
    const char* nameReset = looksExecutable(entry.name) ? COLOUR_RESET : "";

    /*
     * The padding is applied to the name alone rather than to the coloured string: the escape
     * sequences are zero-width on screen but count towards printf's field width, so a coloured
     * "%-24s" would come out short by however many bytes the colour codes take.
     */
    char padded[64];
    snprintf(padded, sizeof(padded), "%-24s", entry.name);

    printf("%s%s%s %s%u%s\n",
                 nameColour, padded, nameReset,
                 COLOUR_SIZE, (unsigned)entry.size, COLOUR_RESET);
}

bool buildTarget(const char* sourcePath, const char* targetArg, char* out, size_t outSize) {
    char resolvedTarget[ShellFs::MAX_PATH];
    if (!ShellFs::resolvePath(targetArg, resolvedTarget, sizeof(resolvedTarget))) {
        return false;
    }

    if (!ShellFs::isDirectory(resolvedTarget)) {
        snprintf(out, outSize, "%s", resolvedTarget);
        return true;
    }

    const char* base = strrchr(sourcePath, '/');
    base = (base != nullptr) ? base + 1 : sourcePath;

    const int written = snprintf(out, outSize, "%s/%s", resolvedTarget, base);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

int parseLineCount(int argc, char** argv, int* outCount) {
    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        *outCount = atoi(argv[2]);
        return 3;
    }
    return 1;
}

// The mixer task is started on first use rather than at shell startup, so an app that never makes
// a sound doesn't pay for the task or hold the codec open.
static bool soundReady = false;

int runScript(const char* resolvedPath, int argc, char** argv) {
    size_t size = 0;
    char* source = ShellFs::readFile(resolvedPath, &size);
    if (source == nullptr) {
        printf("%s: cannot read\n", resolvedPath);
        return 1;
    }

    const int status = Shell::runScriptSource(source, argc, argv);
    free(source);
    return status;
}

namespace {

/**
 * Decides whether an argument should be made absolute before a loaded binary sees it.
 *
 * Only two things qualify: a path that already exists, and a path whose parent directory exists
 * (a file the program is about to create, as in `wget url out.txt`). Anything else is passed
 * through untouched.
 *
 * Being strict here matters: rewriting anything that merely looks like a filename would turn
 * `vistep 3` into `vistep /sdcard/3` and `grep hello f` into `grep /sdcard/hello f`, so the
 * program would see a nonsense argument and behave as though it had been given nothing. Search
 * patterns, numbers and subcommands are all indistinguishable from bare filenames, so existence is
 * the only reliable signal.
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

    char candidate[ShellFs::MAX_PATH];
    if (!ShellFs::resolvePath(argument, candidate, sizeof(candidate))) {
        return false;
    }

    if (ShellFs::exists(candidate)) {
        snprintf(out, outSize, "%s", candidate);
        return true;
    }

    // Not present: only rewrite when it names a file inside a directory that does exist, and when
    // the argument actually looks like a filename rather than a bare word. Requiring either a '/'
    // or an extension keeps `grep hello` and `vistep 3` from being treated as output paths.
    const bool hasSeparator = strchr(argument, '/') != nullptr;
    const char* dot = strrchr(argument, '.');
    const bool hasExtension = dot != nullptr && dot != argument && dot[1] != '\0';
    if (!hasSeparator && !hasExtension) {
        return false;
    }

    char parent[ShellFs::MAX_PATH];
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

    if (!ShellFs::isDirectory(parent)) {
        return false;
    }

    snprintf(out, outSize, "%s", candidate);
    return true;
}

// How often the pump loop below checks for a new keystroke while a loaded binary runs.
constexpr uint32_t ELF_PUMP_INTERVAL_MS = 50;

} // namespace

int runElf(const char* resolvedPath, int argc, char** argv) {
    // Arguments that look like relative paths are made absolute before the binary sees them.
    //
    // A loaded binary calls fopen() directly, and ESP-IDF has no per-process working directory for
    // that to resolve against: there is no chdir() at all. The shell's own cwd lives in ShellFs
    // and means nothing to libc, so `./grep.elf x test.sh` would have the binary open "test.sh"
    // relative to the filesystem root and fail. Rewriting here is what lets unmodified POSIX
    // programs work with relative paths.
    //
    // An argument is rewritten if it either names something that exists, or looks like a plain
    // filename that a program might be about to create: `wget url out.txt` has to work as well as
    // `grep x in.txt`, and the output file does not exist yet by definition.
    //
    // Arguments containing a character no filename would carry (':' from a URL, '*' from a
    // pattern, whitespace) are left alone, so URLs and search patterns pass through untouched.
    char* rewritten[32];
    char storage[8][ShellFs::MAX_PATH];
    int stored = 0;

    const int passedArgc = (argc < static_cast<int>(sizeof(rewritten) / sizeof(rewritten[0])))
        ? argc
        : static_cast<int>(sizeof(rewritten) / sizeof(rewritten[0]));

    for (int i = 0; i < passedArgc; i++) {
        rewritten[i] = argv[i];

        // argv[0] is the binary itself, already resolved by the caller.
        if (i == 0 || stored >= 8) {
            continue;
        }

        if (shouldMakeAbsolute(argv[i], storage[stored], ShellFs::MAX_PATH)) {
            rewritten[i] = storage[stored];
            stored++;
        }
    }

    // argv[0] is given as the resolved path too, so a binary that reopens itself still works.
    rewritten[0] = const_cast<char*>(resolvedPath);

    // $PWD is exported so a binary can undo the argument rewriting above when it needs the name the
    // user actually typed. `tar cf a.tar test.sh` receives /sdcard/test.sh, and storing that in the
    // archive would extract as sdcard/test.sh under the destination; tar strips this prefix to
    // recover "test.sh". Only tools that record or print paths need it; everything else just opens
    // what it is given. There is no per-process environment here, so this leaks into the shell's
    // own, which is harmless: the shell reads its cwd from ShellFs, never from getenv.
    setenv("PWD", ShellFs::cwd(), 1);

    // Output written by the binary should appear as it is produced rather than sitting in a buffer
    // until the binary happens to exit.
    fflush(stdout);

    /*
     * Runs the binary as its own app instance (its own task and stack, loaded and run by
     * app-module's own AppLoaderApi for APP_LOCATION_PATH) rather than in this one's process.
     * Its stdio is piped through two AppStreams: this task feeds stdinStream from the keyboard
     * and drains stdoutStream to the screen below, the same way a real shell's parent process
     * would. Stderr is aliased onto stdoutStream (see the bind below) so it keeps real order.
     */
    static uint8_t stdinBuffer[256];
    static uint8_t stdoutBuffer[1024];
    AppStream stdinStream {};
    AppStream stdoutStream {};

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);

    AppStreamBinding bindings[] = {
        { STDIN_FILENO, &stdinStream, stdinBuffer, sizeof(stdinBuffer), &eventGroup },
        { STDOUT_FILENO, &stdoutStream, stdoutBuffer, sizeof(stdoutBuffer), &eventGroup },
    };

    AppEventSubscription eventSub {};
    app_event_subscribe(&eventSub, &eventGroup);

    AppLocation location { APP_LOCATION_PATH, const_cast<char*>(resolvedPath) };
    AppInstanceId childId = 0;
    error_t result = app_execute_for_result_with_streams(
        location, AppStackConfig {}, passedArgc, rewritten,
        bindings, sizeof(bindings) / sizeof(bindings[0]),
        app_scheduler_current_app_id(), &childId);
    if (result == ERROR_NONE) {
        app_stream_bind_alias_fd(&stdoutStream, STDERR_FILENO);
    }

    int exitCode;
    if (result != ERROR_NONE) {
        // Covers both "not a valid/runnable ELF" and "references a symbol the firmware does not
        // export"; the loader logs specifics to the serial console.
        printf("%s: failed to load (see serial log for details)\n", argv[0]);
        exitCode = 126;
    } else {
        /*
         * Pumps input to the child and its output back out until it exits.
         *
         * Polls this app's own stdin rather than blocking on it: a plain read() would also block
         * forever once the child stops reading it (a non-interactive tool, or one that exits before
         * draining what was typed ahead), leaving no way to notice the child has exited. A bounded
         * app_io_await() doubles as that periodic check.
         */
        int32_t childResult = 2; // AppResultEventData's "Error", if the loop below somehow ends
                                  // without ever observing the matching APP_EVENT_RESULT
        bool childDone = false;
        bool ownStdinClosed = false;
        uint8_t drain[256];

        while (!childDone) {
            bool waited = false;
            if (!ownStdinClosed) {
                const error_t awaited = app_io_await(STDIN_FILENO, APP_FILE_WAIT_READABLE, pdMS_TO_TICKS(ELF_PUMP_INTERVAL_MS));
                waited = true;
                if (awaited == ERROR_NONE) {
                    char ch = 0;
                    const ssize_t n = app_io_read(STDIN_FILENO, &ch, 1);
                    if (n == 1) {
                        app_stream_write(&stdinStream, &ch, 1);
                    } else {
                        // The terminal running this shell hung up (touch-to-exit): propagate that to
                        // the child the same way, since it never sees this app's own stdin directly.
                        app_stream_close(&stdinStream);
                        ownStdinClosed = true;
                    }
                }
            }
            if (!waited) {
                vTaskDelay(pdMS_TO_TICKS(ELF_PUMP_INTERVAL_MS));
            }

            size_t n;
            while ((n = app_stream_read(&stdoutStream, drain, sizeof(drain))) > 0) {
                printf("%.*s", static_cast<int>(n), reinterpret_cast<const char*>(drain));
            }

            AppEvent event {};
            while (app_event_poll(&eventSub, &event) == ERROR_NONE) {
                if (event.type == APP_EVENT_RESULT && event.result.launch_id == childId) {
                    childResult = event.result.result;
                    childDone = true;
                }
            }
        }

        // The child may have written its last bytes and exited before the loop above's last read
        // saw them.
        size_t n;
        while ((n = app_stream_read(&stdoutStream, drain, sizeof(drain))) > 0) {
            printf("%.*s", static_cast<int>(n), reinterpret_cast<const char*>(drain));
        }

        // Only app_stream_unsubscribe() guarantees the fd-table binding is gone and no AppFileOps
        // call is still in flight, which is what makes these stack-local AppStreams safe to let go
        // out of scope below; app_stream_close() (already implied by the child's own exit) does
        // neither on its own. Must happen before app_manager_stop() reaps the child, in case that
        // races app_fd_table_teardown()'s own close() of these same fds. The stderr alias fd is
        // closed by that teardown too.
        app_stream_unsubscribe(&stdinStream);
        app_stream_unsubscribe(&stdoutStream);

        app_manager_stop(childId);
        exitCode = childResult;
    }

    app_event_unsubscribe(&eventSub);
    task_event_group_destruct(&eventGroup);

    fflush(stdout);
    return exitCode;
}
