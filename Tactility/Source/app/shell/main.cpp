#include <Tactility/app/shell/LineEditor.h>
#include <Tactility/app/shell/Shell.h>
#include <Tactility/app/shell/ShellFs.h>

#include <app/manifest.h>

#include <tactility/memory.h>
#include <tactility/freertos/freertos.h>

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace tt::app::shell {

/*
 * Cyan, to set it apart from output.
 *
 * The escapes cost no screen columns but do count towards strlen(), so LineEditor measures the
 * prompt with printableWidth() rather than strlen(): every wrap calculation there is in columns.
 */
constexpr auto* PROMPT = "\x1B[96m$\x1B[0m ";

int main(int argc, char* argv[]) {
    // Passed by the terminal app running this one, since there is no ioctl(TIOCGWINSZ) here.
    if (argc > 1) {
        LineEditor::setTerminalColumns(atoi(argv[1]));
    }

    // Unbuffered so typed characters and command output appear as they are written rather than at
    // the next newline or flush. This app's stdout is a pipe, not a tty, so libc would otherwise
    // fully buffer it.
    setvbuf(stdout, nullptr, _IONBF, 0);

    Shell::init();

    puts("Type 'exit' to quit shell.");

    LineEditor editor;
    editor.begin(PROMPT);

    // Blocks until a byte arrives or the terminal app running this one closes its end (touch to
    // exit), which read() reports the same way any closed pipe does: 0, ending this loop. Typing
    // `exit` ends it too: the interpreter's own builtin (sh_builtins.c) sets a flag Shell::execute()
    // has no way to unwind past on its own, so it's checked here after every line.
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        const char* line = nullptr;
        if (editor.feed(c, &line)) {
            Shell::execute(line);
            if (Shell::shouldExit()) {
                break;
            }
            editor.begin(PROMPT);
        }
    }

    const int exitCode = Shell::exitCode();
    Shell::shutdown();
    return exitCode;
}

extern const ::AppManifest manifest = {
    .id = "shell",
    .name = "Shell",
    .category = APP_CATEGORY_SYSTEM,
    .location = {  .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 16 * 1024, .desired_memory_capability = MEMORY_CAPABILITY_INTERNAL },
};

}
