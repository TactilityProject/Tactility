#include <Tactility/app/shell/Shell.h>

#include "Tactility/file/File.h"

#include <Tactility/app/shell/LineEditor.h>
#include <Tactility/app/shell/ShellFs.h>
#include <Tactility/app/shell/commands/CommandSupport.h>
#include <Tactility/app/shell/commands/Commands.h>

#include <app/execute.h>

#include <tactility/filesystem/file_system.h>
#include <tactility/paths.h>

#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

extern "C" {
#include <Tactility/app/shell/shell/sh.h>
}

#include <Tactility/app/shell/ShellBridge.h>

namespace {

// Interpreter state: variables, functions, positional params, $?. Lives for the whole session so
// that `X=1` at the prompt is still set on the next line.
sh_state interpreter;

const Shell::Command COMMANDS[] = {
    { "help",  "Show this list",                 cmdHelp },
    { "which", "Locate a command",               cmdWhich },
    { "ls",    "List directory",                 cmdLs },
    { "cat",   "Print file contents",            cmdCat },
    { "head",  "Show first lines [-n N]",        cmdHead },
    { "tail",  "Show last lines [-n N]",         cmdTail },
    { "wc",    "Count lines, words and bytes",   cmdWc },
    { "mkdir", "Create directory [-p]",          cmdMkdir },
    { "rm",    "Remove file or directory [-r]",  cmdRm },
    { "cp",    "Copy file",                      cmdCp },
    { "mv",    "Move or rename file",            cmdMv },
    { "touch", "Create an empty file",           cmdTouch },
    { "df",    "List mounted filesystems",       cmdDf },
    { "du",    "Show size of a file or tree",    cmdDu },
    { "sh",    "Run a script file",              cmdSh },
    { "date",  "Show the current date and time", cmdDate },
    { "printf","Format and print",              cmdPrintf },
    { "clear", "Clear the screen",               cmdClear },
    { "free",  "Show memory usage",              cmdFree },
};

constexpr int COMMAND_COUNT = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

} // namespace

namespace Shell {

void init() {
    ShellFs::init();
    sh_state_init(&interpreter);

    // The interpreter keeps its own idea of the working directory ($PWD, `cd -`), so seed it from
    // ShellFs rather than letting the two disagree from the first command. HOME is what a bare
    // `cd` returns to, and the interpreter treats it as an error when unset.
    sh_set(&interpreter, "PWD", ShellFs::cwd());
    sh_set(&interpreter, "HOME", ShellFs::cwd());
    sh_set(&interpreter, "PS1", "$ ");

    // TODO: Iterate over installed applications, and/or search across PATH variable entries
    // sh_set(&interpreter, "BIN", binDir);

    // echo/cd/pwd/test/exit are provided by the interpreter itself (sh_builtins.c) and are matched
    // before it ever consults our table, which is correct, since `cd` has to change the shell's
    // own state rather than a child's. Ours were removed to avoid dead code that looks live.
}

void shutdown() {
    sh_state_free(&interpreter);
}

void forEachCommand(void* context, void (*callback)(const Command&, void*)) {
    for (int i = 0; i < COMMAND_COUNT; i++) {
        callback(COMMANDS[i], context);
    }
}

void writeRaw(const void* data, size_t length) {
    printf("%.*s", length, reinterpret_cast<const char*>(data));
}

namespace {

/** Accumulates completion candidates: how many matched, and the prefix they all share. */
struct Candidates {
    const char* word;
    size_t wordLength;
    char common[ShellFs::MAX_PATH];
    bool haveCommon;
    int count;
    // Printed lazily, so a unique match completes silently without disturbing the prompt.
    char first[ShellFs::MAX_PATH];
};

void offerCandidate(Candidates& candidates, const char* name) {
    if (strncmp(name, candidates.word, candidates.wordLength) != 0) {
        return;
    }

    candidates.count++;

    if (!candidates.haveCommon) {
        snprintf(candidates.common, sizeof(candidates.common), "%s", name);
        snprintf(candidates.first, sizeof(candidates.first), "%s", name);
        candidates.haveCommon = true;
        return;
    }

    // Shorten the shared prefix to whatever this candidate still agrees with.
    size_t i = 0;
    while (candidates.common[i] != '\0' && name[i] != '\0' && candidates.common[i] == name[i]) {
        i++;
    }
    candidates.common[i] = '\0';
}

void offerCommand(const Shell::Command& command, void* context) {
    offerCandidate(*static_cast<Candidates*>(context), command.name);
}

void offerEntry(const ShellFs::Entry& entry, void* context) {
    offerCandidate(*static_cast<Candidates*>(context), entry.name);
}

void printCandidateCommand(const Shell::Command& command, void* context) {
    auto* candidates = static_cast<Candidates*>(context);
    if (strncmp(command.name, candidates->word, candidates->wordLength) == 0) {
        printf("%s  ", command.name);
    }
}

void printCandidateEntry(const ShellFs::Entry& entry, void* context) {
    auto* candidates = static_cast<Candidates*>(context);
    if (strncmp(entry.name, candidates->word, candidates->wordLength) == 0) {
        printf("%s%s  ", entry.name, entry.isDirectory ? "/" : "");
    }
}

} // namespace

bool complete(const char* line, char* outSuffix, size_t suffixSize, bool* outListed) {
    *outListed = false;
    outSuffix[0] = '\0';

    // Find the word under the cursor, which is everything after the last space.
    const char* wordStart = strrchr(line, ' ');
    const bool isFirstWord = (wordStart == nullptr);
    wordStart = isFirstWord ? line : wordStart + 1;

    Candidates candidates {};
    candidates.word = wordStart;
    candidates.wordLength = strlen(wordStart);

    // Paths complete against a directory, which may be named in the word itself ("ls /data/fo").
    char directory[ShellFs::MAX_PATH] = {};
    const char* namePart = wordStart;

    if (!isFirstWord) {
        const char* slash = strrchr(wordStart, '/');
        if (slash != nullptr) {
            char prefix[ShellFs::MAX_PATH];
            const size_t prefixLength = static_cast<size_t>(slash - wordStart);
            if (prefixLength >= sizeof(prefix)) {
                return false;
            }
            memcpy(prefix, wordStart, prefixLength);
            prefix[prefixLength] = '\0';

            // A leading "/foo" leaves an empty prefix, which means the root itself.
            if (!ShellFs::resolvePath(prefixLength == 0 ? "/" : prefix, directory, sizeof(directory))) {
                return false;
            }
            namePart = slash + 1;
        } else {
            snprintf(directory, sizeof(directory), "%s", ShellFs::cwd());
        }

        candidates.word = namePart;
        candidates.wordLength = strlen(namePart);
    }

    if (isFirstWord) {
        forEachCommand(&candidates, offerCommand);
    } else if (ShellFs::isRoot(directory)) {
        // The synthetic root lists mounts rather than directory entries.
        file_system_for_each_mounted(&candidates, [](FileSystem* fs, void* context) {
            char path[ShellFs::MAX_PATH];
            if (file_system_get_path(fs, path, sizeof(path)) == ERROR_NONE) {
                const char* name = strrchr(path, '/');
                offerCandidate(*static_cast<Candidates*>(context), (name != nullptr) ? name + 1 : path);
            }
            return true;
        });
    } else {
        ShellFs::listDirectory(directory, &candidates, offerEntry);
    }

    if (candidates.count == 0) {
        return false;
    }

    // Everything matched shares at least the typed word, so append only what is beyond it.
    const size_t commonLength = strlen(candidates.common);
    if (commonLength > candidates.wordLength) {
        snprintf(outSuffix, suffixSize, "%s", candidates.common + candidates.wordLength);
    }

    // A single match is unambiguous: finish it, and add a separator so the next word can be typed.
    if (candidates.count == 1) {
        const size_t used = strlen(outSuffix);
        if (used + 1 < suffixSize) {
            const bool directoryMatch = !isFirstWord && [&] {
                char full[ShellFs::MAX_PATH];
                const int written = snprintf(full, sizeof(full), "%s/%s", directory, candidates.first);
                return written > 0 && static_cast<size_t>(written) < sizeof(full) && ShellFs::isDirectory(full);
            }();
            outSuffix[used] = directoryMatch ? '/' : ' ';
            outSuffix[used + 1] = '\0';
        }
        return true;
    }

    // Several matches and nothing more to add: show what they are.
    if (outSuffix[0] == '\0') {
        printf("\n");
        if (isFirstWord) {
            forEachCommand(&candidates, printCandidateCommand);
        } else if (ShellFs::isRoot(directory)) {
            file_system_for_each_mounted(&candidates, [](FileSystem* fs, void*) {
                char path[ShellFs::MAX_PATH];
                if (file_system_get_path(fs, path, sizeof(path)) == ERROR_NONE) {
                    const char* name = strrchr(path, '/');
                    printf("%s  ", (name != nullptr) ? name + 1 : path);
                }
                return true;
            });
        } else {
            ShellFs::listDirectory(directory, &candidates, printCandidateEntry);
        }
        printf("\n");
        *outListed = true;
    }

    return true;
}

int runCommand(int argc, char** argv, int* found) {
    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(argv[0], COMMANDS[i].name) == 0) {
            *found = 1;
            return COMMANDS[i].function(argc, argv);
        }
    }

    // Not a builtin: try it as a file. A name containing '/' is taken as a path; a bare name is
    // looked up in the app's bundled binaries, which stands in for a PATH search. A bare name that
    // matches nothing there stays a fast "command not found" rather than a directory scan.
    char resolved[ShellFs::MAX_PATH];
    bool haveFile = false;

    if (strchr(argv[0], '/') != nullptr) {
        haveFile = ShellFs::resolvePath(argv[0], resolved, sizeof(resolved)) &&
            ShellFs::exists(resolved) && !ShellFs::isDirectory(resolved);
    } else {
        // TODO: check if there is a binary by iterating over installed applications, and/or search across PATH variable entries
    }

    if (haveFile) {
        *found = 1;
        // ELF binaries and shell scripts are told apart by content rather than by extension,
        // since the filesystem is FAT and carries no execute bit to consult.
        return app_is_executable_path(resolved)
            ? runElf(resolved, argc, argv)
            : runScript(resolved, argc, argv);
    }

    *found = 0;
    return 127;
}

int runScriptSource(const char* source, int argc, char** argv) {
    // Scripts run on their own interpreter state rather than the session's. A script is reached
    // from inside sh_run_string(): the interpreter calls runCommand() for the `sh foo` line while
    // the outer parse is still in progress, and a fresh state keeps the two runs independent.
    //
    // The consequence is that a script cannot see or modify the session's variables. `.` and
    // `source` remain the way to run something in the current shell, which is what they are for.
    sh_state scriptState;
    sh_state_init(&scriptState);

    // Carry the environment-ish basics across so scripts see a sane world.
    if (const char* home = sh_get(&interpreter, "HOME")) {
        sh_set(&scriptState, "HOME", home);
    }
    if (const char* bin = sh_get(&interpreter, "BIN")) {
        sh_set(&scriptState, "BIN", bin);
    }
    sh_set(&scriptState, "PWD", ShellFs::cwd());

    const int status = sh_run_string_args(&scriptState, source, argc, argv);

    sh_state_free(&scriptState);
    return status;
}

void execute(const char* line) {
    // Everything goes through the shell interpreter, so a bare "ls" and a full
    // `for f in *.txt; do wc -l $f; done` take the same path. It calls back into runCommand()
    // via sh_port_run_external() once a command line has been expanded.
    sh_run_string(&interpreter, line);
}

bool shouldExit() {
    return interpreter.exiting != 0;
}

int exitCode() {
    return interpreter.exit_code;
}

} // namespace Shell

// ---------------------------------------------------------------------------
// Bridge for the vendored shell core, which is C and cannot see the above.
// ---------------------------------------------------------------------------

extern "C" {

int shell_bridge_run_command(int argc, char** argv, int* found) {
    if (argc <= 0) {
        *found = 0;
        return 127;
    }
    return Shell::runCommand(argc, argv, found);
}

void shell_bridge_temp_path(int which, char* buf, int bufsz) {
    // Pipeline staging files go in the shared temp directory: the working directory could be a
    // read-only mount, and scratch files appearing wherever the user happens to be standing is
    // unfriendly. Falls back to the cwd only if the temp path is unavailable.
    char directory[ShellFs::MAX_PATH];

    if (paths_get_temp_path(directory, sizeof(directory)) == ERROR_NONE) {
        snprintf(buf, bufsz, "%s/.sh_pipe_%d", directory, which);
    } else {
        snprintf(buf, bufsz, "%s/.sh_pipe_%d", ShellFs::cwd(), which);
    }
}

int shell_bridge_chdir(const char* path) {
    return ShellFs::changeDirectory(path) ? 0 : -1;
}

void shell_bridge_getcwd(char* buf, int bufsz) {
    snprintf(buf, bufsz, "%s", ShellFs::cwd());
}

int shell_bridge_resolve(const char* path, char* buf, int bufsz) {
    return ShellFs::resolvePath(path, buf, static_cast<size_t>(bufsz)) ? 0 : -1;
}

char* shell_bridge_read_file(const char* path, size_t* outSize) {
    char resolved[ShellFs::MAX_PATH];
    if (!ShellFs::resolvePath(path, resolved, sizeof(resolved))) {
        return nullptr;
    }
    return ShellFs::readFile(resolved, outSize);
}

} // extern "C"
