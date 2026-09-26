#include <Tactility/app/shell/Shell.h>
#include <Tactility/app/shell/Run.h>
#include <Tactility/app/shell/ShellFs.h>

#include <app/execute.h>
#include <app/manager.h>

#include <tactility/filesystem/file_system.h>
#include <tactility/filesystem/fs.h>
#include <tactility/paths.h>

#include <sys/stat.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include <Tactility/app/shell/shell/sh.h>
}

#include <Tactility/app/shell/ShellBridge.h>

namespace {

// Interpreter state: variables, functions, positional params, $?. Lives for the whole session so
// that `X=1` at the prompt is still set on the next line.
sh_state interpreter;

// Relays a Shell::Command, built on the fly from a registered AppManifest, out to forEachCommand()'s
// own caller-supplied callback.
struct CommandRelay {
    void* context;
    void (*callback)(const Shell::Command&, void*);
};

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
    struct CommandId { AppId id; };
    std::vector<CommandId> ids;
    // First gather all IDs, so we don't keep the ledger lock while calling callback()
    app_manager_for_each_manifest([](const AppManifest* manifest, void* context) {
        if ((manifest->flags & APP_MANIFEST_FLAG_HEADLESS) != 0
            && manifest->location.type == APP_LOCATION_MEMORY) {
            auto& ids = *static_cast<std::vector<CommandId>*>(context);
            ids.emplace_back();
            memcpy(ids.back().id, manifest->id, sizeof(AppId));
        }
    }, &ids);
    for (const auto& [id] : ids) {
        callback(Command { .name = id, .help = "" }, context);
    }
}

namespace {

/** Accumulates completion candidates: how many matched, and the prefix they all share. */
struct Candidates {
    const char* word;
    size_t wordLength;
    char common[FILE_MAX_PATH_STRING_LENGTH];
    bool haveCommon;
    int count;
    // Printed lazily, so a unique match completes silently without disturbing the prompt.
    char first[FILE_MAX_PATH_STRING_LENGTH];
    // A bare first word only runs registered commands, so a plain file there would complete to
    // something that can't run. Directories are still offered, to continue typing a path into.
    bool directoriesOnly;
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

void offerEntry(const DirectoryEntry* entry, void* context) {
    auto* candidates = static_cast<Candidates*>(context);
    if (candidates->directoriesOnly && !entry->is_directory) {
        return;
    }
    offerCandidate(*candidates, entry->name);
}

void printCandidateCommand(const Shell::Command& command, void* context) {
    auto* candidates = static_cast<Candidates*>(context);
    if (strncmp(command.name, candidates->word, candidates->wordLength) == 0) {
        printf("%s  ", command.name);
    }
}

void printCandidateEntry(const DirectoryEntry* entry, void* context) {
    auto* candidates = static_cast<Candidates*>(context);
    if (candidates->directoriesOnly && !entry->is_directory) {
        return;
    }
    if (strncmp(entry->name, candidates->word, candidates->wordLength) == 0) {
        printf("%s%s  ", entry->name, entry->is_directory ? "/" : "");
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

    // A first word names a command or, like "./tool" or "app/tool", a file to run.
    const bool includeCommands = isFirstWord && strchr(wordStart, '/') == nullptr;

    Candidates candidates {};
    candidates.directoriesOnly = includeCommands;

    // Paths complete against a directory, which may be named in the word itself ("ls /data/fo").
    char directory[FILE_MAX_PATH_STRING_LENGTH] = {};
    const char* namePart = wordStart;

    const char* slash = strrchr(wordStart, '/');
    if (slash != nullptr) {
        char prefix[FILE_MAX_PATH_STRING_LENGTH];
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

    if (includeCommands) {
        forEachCommand(&candidates, offerCommand);
    }
    directory_list(directory, &candidates, offerEntry);

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
            char full[FILE_MAX_PATH_STRING_LENGTH];
            const int written = snprintf(full, sizeof(full), "%s/%s", directory, candidates.first);
            const bool directoryMatch = written > 0 && static_cast<size_t>(written) < sizeof(full) && directory_exists(full);
            outSuffix[used] = directoryMatch ? '/' : ' ';
            outSuffix[used + 1] = '\0';
        }
        return true;
    }

    // Several matches and nothing more to add: show what they are.
    if (outSuffix[0] == '\0') {
        printf("\n");
        if (includeCommands) {
            forEachCommand(&candidates, printCandidateCommand);
        }
        directory_list(directory, &candidates, printCandidateEntry);
        printf("\n");
        *outListed = true;
    }

    return true;
}

int runCommand(int argc, char** argv, int* found) {
    // Resolved via forEachCommand() (iterate registered manifests, filter to headless-safe ones)
    // rather than a direct app_manager_find_manifest() lookup, so completion/help and dispatch stay
    // backed by the exact same set. The match is only captured here (a plain copy - name/help are
    // borrowed pointers into the manifest) and run after forEachCommand() returns, once
    // app_ledger()'s lock (held for the whole enumeration) is released: runFromMemory() starts a
    // real app instance by id, which itself needs the ledger.
    struct Match {
        const char* id;
        bool found;
        Command command;
    };
    Match match { argv[0], false, {} };
    forEachCommand(&match, [](const Command& command, void* context) {
        auto* match = static_cast<Match*>(context);
        if (!match->found && strcmp(command.name, match->id) == 0) {
            match->found = true;
            match->command = command;
        }
    });

    if (match.found) {
        *found = 1;
        return runFromMemory(argv[0], argc, argv);
    }

    // Not a builtin: try it as a file. A name containing '/' is taken as a path; a bare name is
    // looked up in the app's bundled binaries, which stands in for a PATH search. A bare name that
    // matches nothing there stays a fast "command not found" rather than a directory scan.
    char resolved[FILE_MAX_PATH_STRING_LENGTH];
    bool haveFile = false;

    if (strchr(argv[0], '/') != nullptr) {
        haveFile = ShellFs::resolvePath(argv[0], resolved, sizeof(resolved)) &&
            path_exists(resolved) && !directory_exists(resolved);
    } else {
        // TODO: search across PATH variable entries
    }

    if (haveFile) {
        *found = 1;
        // ELF binaries and shell scripts are told apart by content rather than by extension,
        // since the filesystem is FAT and carries no execute bit to consult.
        if (app_is_executable_path(resolved)) {
            return runElf(resolved, argc, argv);
        }
        // A script runs in its own `sh` app instance, so it gets its own task and stack rather
        // than nesting another interpreter on this one's.
        std::vector<char*> shArgv;
        shArgv.push_back(const_cast<char*>("sh"));
        for (int i = 0; i < argc; i++) {
            shArgv.push_back(argv[i]);
        }
        return runFromMemory("sh", static_cast<int>(shArgv.size()), shArgv.data());
    }

    *found = 0;
    return 127;
}

int runScriptSource(const char* source, int argc, char** argv) {
    // Scripts run on their own interpreter state rather than the session's: each runs in its own
    // `sh` app instance, and a fresh state keeps it independent of the interactive session.
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
    char directory[FILE_MAX_PATH_STRING_LENGTH];

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
    char resolved[FILE_MAX_PATH_STRING_LENGTH];
    if (!ShellFs::resolvePath(path, resolved, sizeof(resolved))) {
        return nullptr;
    }

    struct stat info;
    if (stat(resolved, &info) != 0 || info.st_size < 0) {
        return nullptr;
    }

    // file_read_binary() reads into a caller-owned, bounded buffer rather than allocating one
    // itself; the size to allocate is known up front from stat(), plus one byte to NUL-terminate.
    const size_t capacity = static_cast<size_t>(info.st_size);
    auto* buffer = static_cast<char*>(malloc(capacity + 1));
    if (buffer == nullptr) {
        return nullptr;
    }

    size_t size = capacity;
    if (file_read_binary(resolved, reinterpret_cast<uint8_t*>(buffer), &size) != ERROR_NONE) {
        free(buffer);
        return nullptr;
    }

    buffer[size] = '\0';
    if (outSize != nullptr) {
        *outSize = size;
    }
    return buffer;
}

} // extern "C"
