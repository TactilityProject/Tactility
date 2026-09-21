#pragma once

#include <cstddef>

/**
 * Command registry and dispatcher.
 *
 * Replaces `esp_console`, which is not exported to ELF apps. Only six of its entry points were
 * used upstream (init, run, cmd_register, get_completion, get_hint, register_help_command), so a
 * small table plus an argv splitter covers the same ground without needing the component.
 */
namespace Shell {

using CommandFunction = int (*)(int argc, char** argv);

struct Command {
    const char* name;
    const char* help;
    CommandFunction function;
};

/** Registers the builtin command set. */
void init();

/** Silences and releases the audio device, if the shell ever started it. */
void shutdown();

/** Runs `line` through the shell interpreter: expansion, redirection, pipelines and control flow. */
void execute(const char* line);

/**
 * Looks up and runs a single builtin with an already-expanded argv.
 *
 * Called by the interpreter (via sh_port_run_external) once it has finished expanding a command
 * line, so this sees the final arguments with globs, variables and quotes already resolved.
 *
 * @param[out] found 1 if a command of that name exists, 0 otherwise
 * @return the command's exit status, or 127 when not found
 */
int runCommand(int argc, char** argv, int* found);

/**
 * Runs script text with `argv` installed as $0..$N for its duration.
 * @return the script's exit status
 */
int runScriptSource(const char* source, int argc, char** argv);

/** Calls `callback` for each registered command, for `help` and tab completion. */
void forEachCommand(void* context, void (*callback)(const Command& command, void* context));

/**
 * Completes the final word of `line`.
 *
 * The first word completes against command names, any later word against filesystem paths — which
 * is why this lives here rather than in LineEditor: it needs both the command table and ShellFs.
 *
 * On a unique match, `outSuffix` receives the text to append. On several, the common prefix shared
 * by all of them is returned instead (which may be empty), and the candidates are printed.
 *
 * @param[out] outSuffix text to append to the line
 * @param[out] outListed true if candidates were printed, so the caller knows to redraw the prompt
 * @return true if there was anything to complete
 */
bool complete(const char* line, char* outSuffix, size_t suffixSize, bool* outListed);

/** Prints formatted text to the terminal. */
void print(const char* format, ...) __attribute__((format(printf, 1, 2)));

/** Writes a string to the terminal verbatim, without printf interpretation. */
void write(const char* text);

/**
 * Writes exactly `length` bytes to stdout, respecting the current redirect target - unlike
 * fwrite(), which this app's stdio wrapping does not intercept (see AppStdioWrap.cpp), so bytes
 * written through it never reach an AppStream capturing this app's own stdout (e.g. the terminal
 * app piping it to the screen). Safe for a byte that isn't valid C-string content (e.g. a literal
 * NUL), unlike write()/print(), which assume one.
 */
void writeRaw(const void* data, size_t length);

/**
 * True when stdout still points at the terminal rather than at a redirect target.
 *
 * Commands that format output for a screen — line-ending translation, replacing unprintable bytes —
 * must not do so when the output is going into a file.
 */
bool outputIsTerminal();

} // namespace Shell
