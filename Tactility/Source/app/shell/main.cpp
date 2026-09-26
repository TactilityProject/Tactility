#include <Tactility/app/shell/LineEditor.h>
#include <Tactility/app/shell/Shell.h>
#include <Tactility/app/shell/ShellFs.h>
#include <Tactility/app/shell/Run.h>

#include <app/io.h>
#include <app/manifest.h>

#include <tactility/filesystem/fs.h>

#include <tactility/memory.h>
#include <tactility/freertos/freertos.h>

#include <cstdio>
#include <new>
#include <unistd.h>

namespace tt::app::shell {

/*
 * Cyan, to set it apart from output.
 *
 * The escapes cost no screen columns but do count towards strlen(), so LineEditor measures the
 * prompt with printableWidth() rather than strlen(): every wrap calculation there is in columns.
 */
constexpr auto* PROMPT = "\x1B[96m$\x1B[0m ";

// Mirrors a real terminal's ioctl(fd, TIOCGWINSZ, ...) idiom: try each stdio fd in turn, since
// whichever ones are piped to the terminal app depends on the caller (see app_stream_bind_alias_fd
// in terminal/Shell.cpp, which aliases stderr onto the same stream as stdout).
int getTerminalColumns() {
    AppWindowSize size {};
    if (app_io_ioctl(STDIN_FILENO, APP_IOCTL_GET_WINDOW_SIZE, &size) == ERROR_NONE ||
        app_io_ioctl(STDOUT_FILENO, APP_IOCTL_GET_WINDOW_SIZE, &size) == ERROR_NONE ||
        app_io_ioctl(STDERR_FILENO, APP_IOCTL_GET_WINDOW_SIZE, &size) == ERROR_NONE) {
        return size.columns;
    }
    return 0;
}

int main(int, char*[]) {
    LineEditor::setTerminalColumns(getTerminalColumns());

    // Unbuffered so typed characters and command output appear as they are written rather than at
    // the next newline or flush. This app's stdout is a pipe, not a tty, so libc would otherwise
    // fully buffer it.
    setvbuf(stdout, nullptr, _IONBF, 0);

    Shell::init();

    puts("Type 'exit' to quit shell.");

    // Heap-allocated: its history buffer is several KB, which the interpreter needs on the stack
    constexpr MemoryPolicy policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
    void* editorMemory = memory_alloc_with_policy(sizeof(LineEditor), &policy);
    if (editorMemory == nullptr) {
        puts("shell: out of memory");
        Shell::shutdown();
        return 1;
    }
    auto* editor = new (editorMemory) LineEditor();
    editor->begin(PROMPT);

    // Blocks until a byte arrives or the terminal app running this one closes its end (touch to
    // exit), which read() reports the same way any closed pipe does: 0, ending this loop. Typing
    // `exit` ends it too: the interpreter's own builtin (sh_builtins.c) sets a flag Shell::execute()
    // has no way to unwind past on its own, so it's checked here after every line.
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        const char* line = nullptr;
        if (editor->feed(c, &line)) {
            Shell::execute(line);
            // Print \n if output didn't end with it, to make the shell more readable.
            endLineIfNeeded();
            if (Shell::shouldExit()) {
                break;
            }
            editor->begin(PROMPT);
        }
    }

    editor->~LineEditor();
    memory_free(editorMemory);

    const int exitCode = Shell::exitCode();
    Shell::shutdown();
    return exitCode;
}

extern const ::AppManifest manifest = {
    .id = "shell",
    .name = "Shell",
    .category = APP_CATEGORY_SYSTEM,
    .location = {  .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 6144, .desired_memory_capability = 0 },
};

static int32_t shMain(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage: sh <script> [args...]\n");
        return 1;
    }

    char resolved[FILE_MAX_PATH_STRING_LENGTH];
    if (!ShellFs::resolvePath(argv[1], resolved, sizeof(resolved))) {
        printf("sh: %s: path too long\n", argv[1]);
        return 1;
    }

    if (!path_exists(resolved)) {
        printf("sh: %s: not found\n", argv[1]);
        return 1;
    }

    // Shift so the script sees itself as $0 and its own arguments as $1..$N.
    return runScript(resolved, argc - 1, argv + 1);
}

extern const ::AppManifest sh_manifest = {
    .id = "sh",
    .name = "sh",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(shMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN | APP_MANIFEST_FLAG_HEADLESS,
    .stack = { .depth = 8192, .desired_memory_capability = 0 },
};

}
