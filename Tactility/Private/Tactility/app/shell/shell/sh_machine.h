// Interpreter execution machine.
//
// Execution and expansion run on an explicit stack of frames instead of the
// native call stack. Each frame is the state of one unfinished construct (a
// loop, a command, a word being expanded, ...). The machine repeatedly steps
// the top frame: a frame either pushes a child frame and waits for its result,
// or finishes and hands its result to the frame below. The native stack stays
// the same size regardless of how deeply a script nests.
//
// Frames are stored in chunks that never move, so a frame may hand a child a
// pointer to one of its own members. A frame must have no side effects before
// its first step(): frames are moved into place, and a moved-from frame is
// destroyed without ever having run.
#pragma once

#include "sh.h"
#include "sh_parse.h"
#include "sh_port.h"

#include <tactility/memory.h>

#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Maximum number of frames. Exceeding it (or running out of memory for frames) aborts the run with
// "nesting too deep" instead of exhausting memory.
#define SH_MAX_FRAMES 1024

namespace sh {

using Fields = std::vector<std::string>;

/** What a finished frame hands to the frame that pushed it. */
struct Result {
    int status = 0;
    std::string text;   // single-string expansions and command substitution output
    Fields fields;      // word expansion
};

/** The field currently being built during expansion. */
struct FieldBuilder {
    std::string text;
    bool started = false;

    void put(char c) {
        text.push_back(c);
        started = true;
    }
    /** Ends the field if it has begun. */
    void emit(Fields& out);
    /** Ends the field even when empty (an IFS delimiter boundary). */
    void emitForced(Fields& out);
};

/** Control-flow flags that command substitution and pipeline stages keep local. */
struct FlowFlags {
    int exiting, exitCode, brk, cont, returning, returnCode;

    static FlowFlags save(const sh_state& st) {
        return { st.exiting, st.exit_code, st.brk, st.cont, st.returning, st.return_code };
    }
    void restore(sh_state& st) const {
        st.exiting = exiting; st.exit_code = exitCode;
        st.brk = brk; st.cont = cont;
        st.returning = returning; st.return_code = returnCode;
    }
};

/** Redirections applied around a command. Restores the streams and removes here-doc files when destroyed. */
struct Redirections {
    sh_redir_saved io {};
    bool active = false;
    std::vector<std::string> paths;       // expanded filenames
    std::vector<std::string> tempFiles;   // here-doc bodies

    Redirections() = default;
    Redirections(Redirections&&) = default;
    ~Redirections();
    void restore();
};

class Machine;

enum class Step { Call, Return };

// region Frames

/** Lex, parse and execute a script. Also implements `eval` and `source`/`.`. */
struct RunProgram {
    enum class Mode { Plain, Eval, Source };

    std::string source;
    Mode mode = Mode::Plain;
    std::optional<Fields> positional;   // Source: replaces $@ for the duration
    node* root = nullptr;
    bool started = false;
    char** savedPositional = nullptr;
    int savedPositionalCount = 0;
    bool positionalSwapped = false;
    sh_state* state = nullptr;

    RunProgram(std::string source, Mode mode) : source(std::move(source)), mode(mode) {}
    RunProgram(RunProgram&&) = default;
    ~RunProgram();
    Step step(Machine& m);

private:
    Step finish(Machine& m, int status);
    void restorePositional(sh_state& st);
};

/** Any AST node: compound-command redirects, negation and errexit around the node's own frame. */
struct ExecNode {
    node* n;
    int exempt;
    enum class Phase { Start, Redirected, Body } phase = Phase::Start;
    std::unique_ptr<Redirections> redirections;   // only for a compound command with redirects

    ExecNode(node* n, int exempt) : n(n), exempt(exempt) {}
    Step step(Machine& m);

private:
    Step body(Machine& m);
    Step finish(Machine& m, int status);
};

struct ExecList {
    node* n;
    int exempt;
    int index = 0;
    int status = 0;
    bool started = false;
    Step step(Machine& m);
};

struct ExecAndOr {
    node* n;
    int exempt;
    enum class Phase { Start, Left, Right } phase = Phase::Start;
    Step step(Machine& m);
};

struct ExecIf {
    node* n;
    int exempt;
    int clause = 0;
    enum class Phase { Start, Condition, Tail } phase = Phase::Start;
    Step step(Machine& m);
};

struct ExecWhile {
    node* n;
    int exempt;
    int status = 0;
    sh_state* state = nullptr;   // set once loop_depth has been incremented
    enum class Phase { Start, Condition, Body } phase = Phase::Start;

    ExecWhile(node* n, int exempt) : n(n), exempt(exempt) {}
    ExecWhile(ExecWhile&&) = default;
    ~ExecWhile();
    Step step(Machine& m);
};

struct ExecFor {
    node* n;
    int exempt;
    Fields items;
    int word = 0;
    size_t item = 0;
    int status = 0;
    sh_state* state = nullptr;   // set once loop_depth has been incremented
    enum class Phase { Start, Expanding, Body } phase = Phase::Start;

    ExecFor(node* n, int exempt) : n(n), exempt(exempt) {}
    ExecFor(ExecFor&&) = default;
    ~ExecFor();
    Step step(Machine& m);

private:
    Step next(Machine& m);
};

struct ExecCase {
    node* n;
    int exempt;
    std::string subject;
    int clause = 0;
    int pattern = 0;
    enum class Phase { Start, Subject, Pattern, Tail } phase = Phase::Start;
    Step step(Machine& m);

private:
    Step nextPattern(Machine& m);
};

/** `{ list; }` or a `( list )` subshell, which snapshots and restores the shell state. */
struct ExecGroup {
    node* n;
    int exempt;
    bool started = false;
    sh_state* state = nullptr;   // set once a subshell snapshot has been taken
    sh_var* savedVars = nullptr;
    char** savedPositional = nullptr;
    int savedPositionalCount = 0;
    char* savedArg0 = nullptr;
    std::string savedCwd;
    char* savedLogicalCwd = nullptr;   // sh_state::cwd, which `cd` replaces
    FlowFlags savedFlow {};
    int savedErrexit = 0;
    int savedNounset = 0;

    ExecGroup(node* n, int exempt) : n(n), exempt(exempt) {}
    ExecGroup(ExecGroup&&) = default;
    ~ExecGroup();
    Step step(Machine& m);
};

/** A pipeline, emulated with temp files between stages. */
struct ExecPipe {
    node* n;
    int exempt;
    std::string tempA;
    std::string tempB;
    int stage = 0;
    int status = 0;
    const char* previousOutput = nullptr;
    std::unique_ptr<sh_redir_saved> stageIo;
    bool stageRedirected = false;
    FlowFlags savedFlow {};
    bool started = false;

    ExecPipe(node* n, int exempt) : n(n), exempt(exempt) {}
    ExecPipe(ExecPipe&&) = default;
    ~ExecPipe();
    Step step(Machine& m);

private:
    Step startStage(Machine& m);
};

/** A simple command: assignments, word expansion, redirects, then a function, builtin or external command. */
struct ExecSimple {
    node* n;
    int exempt;
    enum class Phase {
        Start, PureAssign, PureRedirected, Words, EmptyAssign, PrefixAssign, Redirected, Ran
    } phase = Phase::Start;
    int index = 0;
    int emptyStatus = 0;
    Fields argv;
    std::unique_ptr<Redirections> redirections;   // only for a command with redirects

    struct Binding {
        std::string name;
        std::optional<std::string> value;
        std::optional<std::string> environment;
    };
    std::vector<Binding> bindings;   // prefix assignments to undo afterwards
    sh_state* state = nullptr;       // set while bindings are applied

    ExecSimple(node* n, int exempt) : n(n), exempt(exempt) {}
    ExecSimple(ExecSimple&&) = default;
    ~ExecSimple();
    Step step(Machine& m);

private:
    Step expandAssignment(Machine& m);
    void applyAssignment(Machine& m, const std::string& value);
    Step redirect(Machine& m, Phase next);
    Step run(Machine& m);
    Step finish(Machine& m, int status);
    void restoreBindings();
};

/** A shell function call with its own positional parameters and `local` scope. */
struct CallFunction {
    sh_func* function;
    Fields argv;
    int exempt;
    char** savedPositional = nullptr;
    int savedPositionalCount = 0;
    int previousReturning = 0;
    sh_state* state = nullptr;   // set once the call has been entered

    CallFunction(sh_func* function, Fields argv, int exempt) : function(function), argv(std::move(argv)), exempt(exempt) {}
    CallFunction(CallFunction&&) = default;
    ~CallFunction();
    Step step(Machine& m);
};

/** Expands a node's redirect words and applies them into the owner's Redirections. Status 0 on success, 1 on failure. */
struct ApplyRedirects {
    node* n;
    Redirections* target;
    int index = 0;
    bool waiting = false;

    struct Item {
        sh_rd_op op;
        int fd;
        int dupfd;
        int path;      // index into target->paths or target->tempFiles, -1 for none
        bool temp;
    };
    std::vector<Item> items;

    Step step(Machine& m);

private:
    void writeHeredoc(const std::string& body);
};

/** `$(...)`: runs a command with stdout captured to a temp file, then reads the output back. */
struct CommandSubst {
    std::string command;
    std::string tempFile;
    std::unique_ptr<sh_redir_saved> io;
    bool redirected = false;
    bool started = false;
    FlowFlags savedFlow {};

    explicit CommandSubst(std::string command) : command(std::move(command)) {}
    CommandSubst(CommandSubst&&) = default;
    ~CommandSubst();
    Step step(Machine& m);

private:
    Step finish(Machine& m, std::string output);
};

/** Expands one raw word: into fields (Word), a single string (Single) or a here-doc body (Heredoc). */
struct Expand {
    enum class Mode { Word, Single, Heredoc };

    std::string raw;
    Mode mode;
    bool allowSplit = false;
    bool allowTilde = false;
    bool quotedContext = false;

    FieldBuilder field;
    Fields out;
    size_t i = 0;
    bool started = false;
    bool inDoubleQuote = false;

    enum class Pending { None, Arithmetic, CommandSubstitution, Brace, Backtick } pending = Pending::None;
    bool pendingSplit = false;

    static Expand word(std::string raw);
    static Expand single(std::string raw, bool allowTilde = true, bool quotedContext = false);
    static Expand heredoc(std::string raw);

    Step step(Machine& m);

private:
    char at(size_t k) const { return k < raw.size() ? raw[k] : '\0'; }
    void resume(Machine& m);
    bool scanWord(Machine& m);
    bool scanHeredoc(Machine& m);
    bool dollar(Machine& m, bool split, bool quoted);
    Step finish(Machine& m);
};

/** The interior of a `${...}` expression, appending into the owning Expand's field. */
struct ExpandBrace {
    std::string content;
    bool quoted;
    bool split;
    FieldBuilder* field;
    Fields* out;

    enum class Phase { Start, Strip, Plus, Minus, Assign, Error } phase = Phase::Start;
    std::string name;
    bool longest = false;
    char stripKind = 0;
    std::string value;     // the parameter's value when the operator doesn't need its word

    Step step(Machine& m);

private:
    Step start(Machine& m);
};

// endregion

using Frame = std::variant<RunProgram, ExecNode, ExecList, ExecAndOr, ExecIf, ExecWhile, ExecFor, ExecCase,
                           ExecGroup, ExecPipe, ExecSimple, CallFunction, ApplyRedirects, CommandSubst,
                           Expand, ExpandBrace>;

/** A stack of frames in fixed chunks, so frames never move once pushed. */
class FrameStack {
    static constexpr size_t CHUNK_SIZE = 8;

    struct Chunk {
        Chunk* previous;
        size_t count;
        alignas(Frame) unsigned char storage[CHUNK_SIZE * sizeof(Frame)];

        Frame* at(size_t index) { return std::launder(reinterpret_cast<Frame*>(storage) + index); }
    };

    Chunk* top = nullptr;
    Chunk* spare = nullptr;   // one emptied chunk is kept, so a loop at a chunk boundary doesn't allocate every iteration
    size_t count = 0;

public:
    FrameStack() = default;
    FrameStack(const FrameStack&) = delete;
    FrameStack& operator=(const FrameStack&) = delete;
    ~FrameStack();

    /** @return false when no memory is available for the frame */
    bool push(Frame&& frame);
    void pop();
    Frame& back() { return *top->at(top->count - 1); }
    size_t size() const { return count; }
    bool empty() const { return count == 0; }
};

class Machine {
public:
    explicit Machine(sh_state& st) : st(st) {}

    sh_state& st;
    /** The result of the frame that finished most recently. */
    Result result;

    /** Runs the root frame to completion and returns its status. */
    int run(Frame root);

    /** Pushes a child frame, which runs before the calling frame is stepped again. */
    Step call(Frame frame) {
        pending.emplace(std::move(frame));
        return Step::Call;
    }

    Step done(int status) {
        result = Result { .status = status, .text = {}, .fields = {} };
        return Step::Return;
    }

    Step done(std::string text) {
        result = Result { .status = 0, .text = std::move(text), .fields = {} };
        return Step::Return;
    }

    Step done(Fields fields) {
        result = Result { .status = 0, .text = {}, .fields = std::move(fields) };
        return Step::Return;
    }

private:
    FrameStack frames;
    std::optional<Frame> pending;
};

// region Expansion helpers (sh_expand.cpp)

const char* get_ifs(sh_state* st);
void append_val(FieldBuilder& b, Fields& out, const char* v, bool split, const char* ifs);
const char* param_raw(sh_state* st, const char* name, char** owned);
bool nounset_fire(sh_state* st, const char* name, const char* val);
void expand_at_star(sh_state* st, bool star, bool quoted, FieldBuilder& b, Fields& out, bool split, const char* ifs);
bool is_var_name(const char* name);

// endregion

} // namespace sh
