// Execution: the frame machine plus the frames for programs, commands and
// control flow. See sh_machine.h for how frames interact.
//
// Frames that change shell state for their duration (loop depth, a subshell
// snapshot, a function's positional params, prefix assignments, redirected
// streams) undo it in their destructor. That covers both normal completion
// and the machine unwinding everything after "nesting too deep".
#include <Tactility/app/shell/shell/sh_machine.h>

#include <Tactility/app/shell/ShellBridge.h>
#include <Tactility/app/shell/shell/sh_builtins.h>
#include <Tactility/app/shell/shell/sh_glob.h>
#include <Tactility/app/shell/shell/sh_lex.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sh {

namespace {

constexpr MemoryPolicy FRAME_MEMORY_POLICY = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };

// Prefix assignments (`A=1 B=2 cmd`) beyond this many are not applied.
constexpr int MAX_PREFIX_ASSIGNMENTS = 32;

// Deep-copy / free the variable list for subshell isolation.
sh_var* clone_vars(sh_var* v)
{
    sh_var* head = nullptr;
    sh_var** tail = &head;
    for (; v; v = v->next) {
        auto* c = static_cast<sh_var*>(malloc(sizeof(sh_var)));
        c->name = strdup(v->name);
        c->value = strdup(v->value);
        c->exported = v->exported;
        c->next = nullptr;
        *tail = c;
        tail = &c->next;
    }
    return head;
}

void free_vars(sh_var* v)
{
    while (v) {
        sh_var* next = v->next;
        free(v->name);
        free(v->value);
        free(v);
        v = next;
    }
}

void free_positional(char** pos, int npos)
{
    for (int i = 0; i < npos; i++) free(pos[i]);
    free(pos);
}

/** NULL-terminated argv pointing into `fields`, for the C APIs. */
std::vector<char*> to_argv(Fields& fields)
{
    std::vector<char*> argv;
    argv.reserve(fields.size() + 1);
    for (auto& field : fields) argv.push_back(field.data());
    argv.push_back(nullptr);
    return argv;
}

std::string temp_path(int which)
{
    char buffer[512];
    sh_port_tmpfile(which, buffer, sizeof(buffer));
    return buffer;
}

// Handle break/continue after running a loop body. Returns true if the loop
// should stop, false if it should continue to the next iteration.
bool loop_control(sh_state& st)
{
    if (st.exiting || st.returning) return true;
    if (st.brk) { st.brk--; return true; }        // break; outer loops see remaining
    if (st.cont) { st.cont--; return st.cont != 0; }
    return false;
}

/** The variable name of a "NAME=raw" assignment word. */
std::string assignment_name(const char* raw)
{
    const char* eq = strchr(raw, '=');
    size_t length = eq ? static_cast<size_t>(eq - raw) : strlen(raw);
    return std::string(raw, std::min<size_t>(length, 127));
}

const char* assignment_value(const char* raw)
{
    const char* eq = strchr(raw, '=');
    return eq ? eq + 1 : "";
}

} // namespace

// region Support types

void FieldBuilder::emit(Fields& out)
{
    if (started) out.push_back(text);
    text.clear();
    started = false;
}

void FieldBuilder::emitForced(Fields& out)
{
    out.push_back(text);
    text.clear();
    started = false;
}

Redirections::~Redirections()
{
    restore();
    for (auto& path : tempFiles) remove(path.c_str());
}

void Redirections::restore()
{
    if (active) {
        sh_redir_restore(&io);
        active = false;
    }
}

FrameStack::~FrameStack()
{
    while (!empty()) pop();
    if (spare != nullptr) memory_free(spare);
}

bool FrameStack::push(Frame&& frame)
{
    if (top == nullptr || top->count == CHUNK_SIZE) {
        Chunk* chunk = spare;
        spare = nullptr;
        if (chunk == nullptr) {
            chunk = static_cast<Chunk*>(memory_alloc_with_policy(sizeof(Chunk), &FRAME_MEMORY_POLICY));
            if (chunk == nullptr) return false;
        }
        chunk->previous = top;
        chunk->count = 0;
        top = chunk;
    }
    new (top->storage + top->count * sizeof(Frame)) Frame(std::move(frame));
    top->count++;
    count++;
    return true;
}

void FrameStack::pop()
{
    back().~Frame();
    top->count--;
    count--;
    if (top->count == 0) {
        Chunk* emptied = top;
        top = emptied->previous;
        if (spare != nullptr) memory_free(spare);
        spare = emptied;
    }
}

int Machine::run(Frame root)
{
    if (!frames.push(std::move(root))) {
        fprintf(stderr, "sh: out of memory\n");
        st.last_status = 2;
        return 2;
    }
    while (!frames.empty()) {
        Step step = std::visit([this](auto& frame) { return frame.step(*this); }, frames.back());
        if (step == Step::Return) {
            frames.pop();
            continue;
        }
        bool pushed = frames.size() < SH_MAX_FRAMES && frames.push(std::move(*pending));
        pending.reset();
        if (!pushed) {
            fprintf(stderr, "sh: nesting too deep\n");
            // Popping runs every frame's cleanup: streams, temp files, scopes and loop depth are restored.
            while (!frames.empty()) frames.pop();
            st.brk = 0;
            st.cont = 0;
            st.returning = 0;
            st.last_status = 2;
            return 2;
        }
    }
    return result.status;
}

// endregion

// region RunProgram

RunProgram::~RunProgram()
{
    sh_free_node(root);
    if (state != nullptr) restorePositional(*state);
}

void RunProgram::restorePositional(sh_state& st)
{
    if (!positionalSwapped) return;
    positionalSwapped = false;
    free_positional(st.pos, st.npos);
    st.pos = savedPositional;
    st.npos = savedPositionalCount;
}

Step RunProgram::step(Machine& m)
{
    sh_state& st = m.st;
    if (started) {
        int status = m.result.status;
        sh_free_node(root);
        root = nullptr;
        return finish(m, st.exiting ? st.exit_code : status);
    }
    started = true;
    state = &st;

    if (positional.has_value()) {
        // Extra `source` arguments set the positional params for the duration (dash behavior).
        savedPositional = st.pos;
        savedPositionalCount = st.npos;
        st.pos = nullptr;
        st.npos = 0;
        auto argv = to_argv(*positional);
        sh_set_positional(&st, nullptr, argv.data(), static_cast<int>(positional->size()));
        positionalSwapped = true;
    }

    st.parse_error = 0;
    sh_toklist tl;
    if (sh_lex(source.c_str(), &tl) != 0) {
        fprintf(stderr, "sh: syntax error (unterminated quote)\n");
        st.parse_error = 1;
        st.last_status = 2;
        return finish(m, 2);
    }
    const char* err = nullptr;
    root = sh_parse(&tl, &err);
    sh_toklist_free(&tl);
    if (!root) {
        fprintf(stderr, "sh: syntax error: %s\n", err ? err : "parse error");
        st.parse_error = 1;
        st.last_status = 2;
        return finish(m, 2);
    }
    return m.call(ExecNode(root, 0));
}

Step RunProgram::finish(Machine& m, int status)
{
    sh_state& st = m.st;
    if (mode == Mode::Source) {
        restorePositional(st);
        // `return` inside a sourced file stops the file, not the whole shell.
        if (st.returning) { status = st.return_code; st.returning = 0; }
    }
    // A syntax error in eval'd or sourced text is fatal in a non-interactive shell
    // (dash aborts with status 2); a runtime failure inside is not.
    if (mode != Mode::Plain && st.parse_error) {
        st.exiting = 1;
        st.exit_code = 2;
    }
    return m.done(status);
}

// endregion

// region ExecNode

Step ExecNode::step(Machine& m)
{
    switch (phase) {
        case Phase::Start:
            // Compound commands may carry a trailing redirect list; apply it around the
            // whole command. Simple commands handle their own redirects internally.
            if (n->kind != N_SIMPLE && n->nredir > 0) {
                phase = Phase::Redirected;
                redirections = std::make_unique<Redirections>();
                return m.call(ApplyRedirects { .n = n, .target = redirections.get() });
            }
            return body(m);
        case Phase::Redirected:
            if (m.result.status != 0) {
                m.st.last_status = 1;
                return m.done(1);
            }
            return body(m);
        case Phase::Body:
            break;
    }
    int status = m.result.status;
    if (redirections) redirections->restore();
    return finish(m, status);
}

Step ExecNode::body(Machine& m)
{
    // A negated pipeline (`! cmd`) is an errexit-exempt context throughout, and
    // its own non-zero result never triggers errexit.
    int inner = exempt || n->negated;
    phase = Phase::Body;
    switch (n->kind) {
        case N_LIST: return m.call(ExecList { .n = n, .exempt = inner });
        case N_ANDOR: return m.call(ExecAndOr { .n = n, .exempt = inner });
        case N_PIPE: return m.call(ExecPipe(n, inner));
        case N_SIMPLE: return m.call(ExecSimple(n, inner));
        case N_IF: return m.call(ExecIf { .n = n, .exempt = inner });
        case N_WHILE: return m.call(ExecWhile(n, inner));
        case N_FOR: return m.call(ExecFor(n, inner));
        case N_CASE: return m.call(ExecCase { .n = n, .exempt = inner });
        case N_GROUP: return m.call(ExecGroup(n, inner));
        case N_FUNCDEF:
            sh_func_define(&m.st, n->func_name, n->body);
            n->body = nullptr;   // ownership transferred; keep sh_free_node(root) from freeing it
            break;
    }
    return finish(m, 0);
}

Step ExecNode::finish(Machine& m, int status)
{
    sh_state& st = m.st;
    if (n->negated) status = (status == 0);
    st.last_status = status;

    // errexit: a command that returns non-zero in a non-exempt context aborts.
    // Structural nodes (list / and-or / brace group) don't trigger — their inner
    // commands already decide — but a subshell `( )` is a command, so it does.
    // Setting `exiting` stops the current command list; a subshell/$() confines
    // it, the top level exits.
    bool structural = n->kind == N_LIST || n->kind == N_ANDOR || (n->kind == N_GROUP && !n->subshell);
    if (st.opt_errexit && !exempt && !n->negated && status != 0 && !structural &&
        !st.exiting && !st.returning && !st.brk && !st.cont) {
        st.exiting = 1;
        st.exit_code = status;
    }
    return m.done(status);
}

// endregion

// region Lists, and-or, if, loops, case

Step ExecList::step(Machine& m)
{
    sh_state& st = m.st;
    if (started) {
        status = m.result.status;
        if (st.exiting || st.returning || st.brk || st.cont) return m.done(status);
        index++;
    }
    started = true;
    if (index < n->nchild) return m.call(ExecNode(n->children[index], exempt));
    return m.done(status);
}

Step ExecAndOr::step(Machine& m)
{
    sh_state& st = m.st;
    switch (phase) {
        case Phase::Start:
            // The left operand is always errexit-exempt (its failure is tested by the
            // &&/||). The right (tail) operand inherits the surrounding context.
            phase = Phase::Left;
            return m.call(ExecNode(n->left, 1));
        case Phase::Left: {
            int status = m.result.status;
            if (st.exiting || st.returning) return m.done(status);
            bool runRight = (n->andor_op == T_AMPAMP) ? status == 0 : status != 0;
            if (!runRight) return m.done(status);
            phase = Phase::Right;
            return m.call(ExecNode(n->right, exempt));
        }
        case Phase::Right:
            break;
    }
    return m.done(m.result.status);
}

Step ExecIf::step(Machine& m)
{
    sh_state& st = m.st;
    switch (phase) {
        case Phase::Start:
            break;
        case Phase::Condition: {
            int c = m.result.status;
            if (st.exiting || st.returning) return m.done(c);
            if (c == 0) {
                phase = Phase::Tail;
                return m.call(ExecNode(n->bodies[clause], exempt));
            }
            clause++;
            break;
        }
        case Phase::Tail:
            return m.done(m.result.status);
    }
    if (clause < n->nclause) {
        phase = Phase::Condition;
        return m.call(ExecNode(n->conds[clause], 1));   // condition: errexit-exempt
    }
    if (n->else_body) {
        phase = Phase::Tail;
        return m.call(ExecNode(n->else_body, exempt));
    }
    return m.done(0);
}

ExecWhile::~ExecWhile()
{
    if (state != nullptr) state->loop_depth--;
}

Step ExecWhile::step(Machine& m)
{
    sh_state& st = m.st;
    switch (phase) {
        case Phase::Start:
            st.loop_depth++;
            state = &st;
            break;
        case Phase::Condition: {
            int c = m.result.status;
            // A break/continue evaluated inside the condition applies to this loop.
            if (st.brk || st.cont) {
                loop_control(st);
                return m.done(status);
            }
            if (n->until) c = (c == 0);   // `until`: loop while cond fails
            if (st.exiting || st.returning || c != 0) return m.done(status);
            phase = Phase::Body;
            return m.call(ExecNode(n->body, exempt));
        }
        case Phase::Body:
            status = m.result.status;
            if (loop_control(st)) return m.done(status);
            break;
    }
    if (st.exiting) return m.done(status);
    phase = Phase::Condition;
    return m.call(ExecNode(n->cond, 1));   // condition: errexit-exempt
}

ExecFor::~ExecFor()
{
    if (state != nullptr) state->loop_depth--;
}

Step ExecFor::step(Machine& m)
{
    sh_state& st = m.st;
    switch (phase) {
        case Phase::Start:
            if (n->for_implicit) {
                // `for x; do` iterates over "$@" (each positional param, unsplit).
                for (int i = 0; i < st.npos; i++) items.emplace_back(st.pos[i]);
                return next(m);
            }
            if (n->for_nword == 0) return next(m);
            phase = Phase::Expanding;
            return m.call(Expand::word(n->for_words[word]));
        case Phase::Expanding:
            for (auto& field : m.result.fields) items.push_back(std::move(field));
            word++;
            if (word < n->for_nword) return m.call(Expand::word(n->for_words[word]));
            return next(m);
        case Phase::Body:
            status = m.result.status;
            if (loop_control(st)) return m.done(status);
            item++;
            break;
    }
    if (item < items.size() && !st.exiting) {
        sh_set(&st, n->for_name, items[item].c_str());
        return m.call(ExecNode(n->body, exempt));
    }
    return m.done(status);
}

/** Enters the loop once the words are expanded. */
Step ExecFor::next(Machine& m)
{
    sh_state& st = m.st;
    st.loop_depth++;
    state = &st;
    phase = Phase::Body;
    if (item < items.size() && !st.exiting) {
        sh_set(&st, n->for_name, items[item].c_str());
        return m.call(ExecNode(n->body, exempt));
    }
    return m.done(status);
}

Step ExecCase::step(Machine& m)
{
    switch (phase) {
        case Phase::Start:
            phase = Phase::Subject;
            return m.call(Expand::single(n->case_word));
        case Phase::Subject:
            subject = std::move(m.result.text);
            return nextPattern(m);
        case Phase::Pattern:
            if (sh_pattern_match(m.result.text.c_str(), subject.c_str())) {
                phase = Phase::Tail;
                return m.call(ExecNode(n->clauses[clause].body, exempt));   // first match wins, no fallthrough
            }
            pattern++;
            return nextPattern(m);
        case Phase::Tail:
            break;
    }
    return m.done(m.result.status);
}

/** Expands the next pattern to try, moving on to the next clause when this one is exhausted. */
Step ExecCase::nextPattern(Machine& m)
{
    while (clause < n->nclause_case && pattern >= n->clauses[clause].npat) {
        clause++;
        pattern = 0;
    }
    if (clause >= n->nclause_case) return m.done(0);
    phase = Phase::Pattern;
    return m.call(Expand::single(n->clauses[clause].pats[pattern]));
}

// endregion

// region Groups and subshells

ExecGroup::~ExecGroup()
{
    if (state == nullptr) return;
    sh_state& st = *state;
    // Restore. Option changes (set -e/-u) inside a subshell must not leak out.
    free_vars(st.vars);
    st.vars = savedVars;
    free_positional(st.pos, st.npos);
    st.pos = savedPositional;
    st.npos = savedPositionalCount;
    free(st.arg0);
    st.arg0 = savedArg0;
    sh_port_chdir(savedCwd.c_str());
    savedFlow.restore(st);
    st.opt_errexit = savedErrexit;
    st.opt_nounset = savedNounset;
}

Step ExecGroup::step(Machine& m)
{
    sh_state& st = m.st;
    if (started) {
        return m.done(m.result.status);
    }
    started = true;
    if (!n->subshell) return m.call(ExecNode(n->body, exempt));

    // Subshell ( list ): snapshot vars, positional params, cwd, and control
    // flags; run isolated; the destructor restores everything but the resulting $?.
    savedVars = clone_vars(st.vars);
    savedPositional = st.pos;
    savedPositionalCount = st.npos;
    savedArg0 = st.arg0 ? strdup(st.arg0) : nullptr;
    st.pos = nullptr;
    st.npos = 0;
    if (savedPositional) {
        st.pos = static_cast<char**>(malloc(savedPositionalCount * sizeof(char*)));
        for (int i = 0; i < savedPositionalCount; i++) st.pos[i] = strdup(savedPositional[i]);
        st.npos = savedPositionalCount;
    }
    char cwd[512];
    sh_port_getcwd(cwd, sizeof(cwd));
    savedCwd = cwd;
    savedFlow = FlowFlags::save(st);
    savedErrexit = st.opt_errexit;
    savedNounset = st.opt_nounset;
    state = &st;
    return m.call(ExecNode(n->body, exempt));
}

// endregion

// region Pipelines

ExecPipe::~ExecPipe()
{
    if (stageRedirected) sh_redir_restore(stageIo.get());
    if (!tempA.empty()) remove(tempA.c_str());
    if (!tempB.empty()) remove(tempB.c_str());
}

Step ExecPipe::step(Machine& m)
{
    if (n->nchild == 1) {
        if (started) return m.done(m.result.status);
        started = true;
        return m.call(ExecNode(n->children[0], exempt));
    }

    if (!started) {
        // Pipelines are emulated with temp files. A stage may itself be a compound
        // command containing another pipeline, so each pipeline gets its own files.
        started = true;
        stageIo = std::make_unique<sh_redir_saved>();
        tempA = temp_path(0);
        tempB = temp_path(1);
        return startStage(m);
    }

    // A stage finished. Each stage runs like a subshell for control flow: an inner
    // `exit`, `return`, or errexit abort stays local to the stage (real shells fork
    // each stage). Only the final stage's status becomes the pipeline's, and
    // errexit is decided at the pipeline level (see ExecNode).
    status = m.result.status;
    savedFlow.restore(m.st);
    sh_redir_restore(stageIo.get());
    stageRedirected = false;
    previousOutput = (stage < n->nchild - 1) ? ((stage % 2) ? tempA.c_str() : tempB.c_str()) : nullptr;
    stage++;
    return startStage(m);
}

Step ExecPipe::startStage(Machine& m)
{
    if (stage >= n->nchild) return m.done(status);

    const char* output = (stage < n->nchild - 1) ? ((stage % 2) ? tempA.c_str() : tempB.c_str()) : nullptr;
    sh_redir_rt items[2];
    int count = 0;
    if (previousOutput) items[count++] = sh_redir_rt { 0, SH_RD_IN, previousOutput, 0 };
    if (output) items[count++] = sh_redir_rt { 1, SH_RD_OUT, output, 0 };
    if (sh_redir_apply(items, count, stageIo.get()) != 0) {
        return m.done(1);
    }
    stageRedirected = true;
    savedFlow = FlowFlags::save(m.st);
    return m.call(ExecNode(n->children[stage], exempt));
}

// endregion

// region Simple commands

ExecSimple::~ExecSimple()
{
    restoreBindings();
}

Step ExecSimple::step(Machine& m)
{
    sh_state& st = m.st;
    switch (phase) {
        case Phase::Start:
            st.cmdsub_ran = 0;
            index = 0;
            if (n->nword == 0) {
                // Pure assignment (no command words): persist to shell state.
                phase = Phase::PureAssign;
                break;
            }
            phase = Phase::Words;
            return m.call(Expand::word(n->words[0]));

        case Phase::PureAssign:
        case Phase::EmptyAssign:
            applyAssignment(m, m.result.text);
            index++;
            break;

        case Phase::PureRedirected:
            if (m.result.status != 0) return m.done(2);
            redirections->restore();
            return m.done(st.cmdsub_ran ? st.cmdsub_status : 0);

        case Phase::Words:
            for (auto& field : m.result.fields) argv.push_back(std::move(field));
            index++;
            if (index < n->nword) return m.call(Expand::word(n->words[index]));
            // A ${v:?} expansion may have aborted mid-word; don't run the command.
            if (st.exiting) return m.done(st.exit_code);
            index = 0;
            if (argv.empty()) {
                // Command expanded to nothing (e.g. `$(exit 42)`); still apply pure
                // assignments. $? is the last command sub's status, or 0 if none ran.
                emptyStatus = st.cmdsub_ran ? st.cmdsub_status : 0;
                phase = Phase::EmptyAssign;
            } else {
                // Temporary bindings: apply assignments, remember old values to restore.
                phase = Phase::PrefixAssign;
            }
            break;

        case Phase::PrefixAssign: {
            applyAssignment(m, m.result.text);
            // A prefix binding is exported to the command's environment (POSIX);
            // setenv so the child inherits it. Restored afterwards.
            const char* value = sh_get(&st, bindings.back().name.c_str());
            setenv(bindings.back().name.c_str(), value ? value : "", 1);
            index++;
            break;
        }

        case Phase::Redirected:
            if (m.result.status != 0) {
                fprintf(stderr, "%s: redirection failed\n", argv[0].c_str());
                return finish(m, 2);   // dash exits a failed redirection with status 2
            }
            return run(m);

        case Phase::Ran:
            return finish(m, m.result.status);
    }

    switch (phase) {
        case Phase::PureAssign:
            if (index < n->nassign) return expandAssignment(m);
            if (st.exiting) return m.done(st.exit_code);   // ${a?msg} in a value is fatal
            if (n->nredir > 0) {
                // Bare redirect (`>file`): open/close the targets, run nothing.
                return redirect(m, Phase::PureRedirected);
            }
            return m.done(st.cmdsub_ran ? st.cmdsub_status : 0);

        case Phase::EmptyAssign:
            if (index < n->nassign) return expandAssignment(m);
            return m.done(emptyStatus);

        case Phase::PrefixAssign:
            if (index < std::min(n->nassign, MAX_PREFIX_ASSIGNMENTS)) {
                const char* raw = n->assigns[index];
                Binding binding { .name = assignment_name(raw), .value = {}, .environment = {} };
                if (const char* old = sh_get(&st, binding.name.c_str())) binding.value = old;
                if (const char* old = getenv(binding.name.c_str())) binding.environment = old;
                bindings.push_back(std::move(binding));
                state = &st;
                return expandAssignment(m);
            }
            if (st.exiting) {
                // fatal ${a?msg} while expanding a prefix binding: the bindings stay applied
                bindings.clear();
                state = nullptr;
                return m.done(st.exit_code);
            }
            if (n->nredir > 0) return redirect(m, Phase::Redirected);
            return run(m);

        default:
            return m.done(0);
    }
}

Step ExecSimple::redirect(Machine& m, Phase next)
{
    phase = next;
    redirections = std::make_unique<Redirections>();
    return m.call(ApplyRedirects { .n = n, .target = redirections.get() });
}

Step ExecSimple::expandAssignment(Machine& m)
{
    return m.call(Expand::single(assignment_value(n->assigns[index])));
}

// Apply a "NAME=raw" assignment with its expanded value. A leading `~` in the value
// tilde-expands via the normal word-start rule (so a=~/src works); the full
// POSIX colon rule (PATH=~/bin:~/sbin) is deliberately left out.
void ExecSimple::applyAssignment(Machine& m, const std::string& value)
{
    const char* raw = n->assigns[index];
    if (!strchr(raw, '=')) return;
    sh_set(&m.st, assignment_name(raw).c_str(), value.c_str());
}

// Run the expanded argv (function, builtin, or external), with redirects applied.
Step ExecSimple::run(Machine& m)
{
    sh_state& st = m.st;
    const std::string& command = argv[0];
    phase = Phase::Ran;

    // Functions override non-special builtins and externals (dash order).
    if (sh_func* function = sh_func_find(&st, command.c_str())) {
        return m.call(CallFunction(function, argv, exempt));
    }

    if (command == "eval") {
        if (argv.size() < 2) return finish(m, 0);
        std::string joined;
        for (size_t i = 1; i < argv.size(); i++) {
            if (i > 1) joined.push_back(' ');
            joined += argv[i];
        }
        return m.call(RunProgram(std::move(joined), RunProgram::Mode::Eval));
    }

    if (command == "." || command == "source") {
        if (argv.size() < 2) {
            fprintf(stderr, "%s: filename argument required\n", command.c_str());
            return finish(m, 2);
        }
        /* Tactility: read through the bridge rather than fopen() directly. The shell's working
         * directory is tracked in ShellFs and is unrelated to the C library's, so a relative path
         * would otherwise resolve against the wrong place - and file access has to take the mount
         * lock, since the display and SD card can share a bus. */
        size_t length = 0;
        char* source = shell_bridge_read_file(argv[1].c_str(), &length);
        if (!source) {
            fprintf(stderr, "%s: %s: cannot open\n", command.c_str(), argv[1].c_str());
            return finish(m, 1);
        }
        RunProgram program(std::string(source, length), RunProgram::Mode::Source);
        free(source);
        if (argv.size() > 2) program.positional = Fields(argv.begin() + 2, argv.end());
        return m.call(std::move(program));
    }

    auto cargv = to_argv(argv);
    int argc = static_cast<int>(argv.size());
    int status = 0;
    if (sh_run_builtin(&st, argc, cargv.data(), &status)) return finish(m, status);

    int found = 0;
    status = sh_port_run_external(argc, cargv.data(), &found);
    if (!found) {
        fprintf(stderr, "\x1B[91m%s: not found\x1B[0m\n", command.c_str());
        status = 127;
    }
    return finish(m, status);
}

Step ExecSimple::finish(Machine& m, int status)
{
    if (redirections) redirections->restore();
    restoreBindings();
    return m.done(status);
}

// Restore temporary bindings.
void ExecSimple::restoreBindings()
{
    if (state == nullptr) return;
    for (auto& binding : bindings) {
        if (binding.value) sh_set(state, binding.name.c_str(), binding.value->c_str());
        else sh_unset(state, binding.name.c_str());
        if (binding.environment) setenv(binding.name.c_str(), binding.environment->c_str(), 1);
        else unsetenv(binding.name.c_str());
    }
    bindings.clear();
    state = nullptr;
}

// endregion

// region Functions

CallFunction::~CallFunction()
{
    if (state == nullptr) return;
    sh_state& st = *state;
    st.returning = previousReturning;
    st.call_depth--;
    sh_scope_pop(&st);
    free_positional(st.pos, st.npos);
    st.pos = savedPositional;
    st.npos = savedPositionalCount;
}

// Call a function: swap in the call args as $1..$#, run the body under a fresh
// `local` scope, honor `return`; the destructor restores the caller's positional params.
Step CallFunction::step(Machine& m)
{
    sh_state& st = m.st;
    if (state != nullptr) {
        return m.done(st.returning ? st.return_code : st.last_status);
    }
    if (st.call_depth >= SH_MAX_CALL_DEPTH) {
        fprintf(stderr, "%s: recursion too deep\n", function->name);
        return m.done(1);
    }
    savedPositional = st.pos;
    savedPositionalCount = st.npos;
    st.pos = nullptr;
    st.npos = 0;
    auto cargv = to_argv(argv);
    sh_set_positional(&st, nullptr, cargv.data() + 1, static_cast<int>(argv.size()) - 1);   // $0 unchanged (dash)

    sh_scope_push(&st);
    st.call_depth++;
    previousReturning = st.returning;
    st.returning = 0;
    state = &st;
    return m.call(ExecNode(function->body, exempt));
}

// endregion

// region Redirections

// Builds the ordered runtime redirect list from a node and applies it. Here-doc
// bodies go to temp files that the target removes when it is destroyed.
Step ApplyRedirects::step(Machine& m)
{
    if (waiting) {
        waiting = false;
        sh_redir* r = &n->redirs[index];
        if (r->kind == R_HEREDOC) {
            writeHeredoc(m.result.text);
        } else {
            target->paths.push_back(std::move(m.result.text));
            sh_rd_op op = (r->kind == R_IN) ? SH_RD_IN : (r->kind == R_APPEND) ? SH_RD_APPEND : SH_RD_OUT;
            items.push_back(Item { op, r->fd, 0, static_cast<int>(target->paths.size()) - 1, false });
        }
        index++;
    }

    while (index < n->nredir && items.size() < 16) {
        sh_redir* r = &n->redirs[index];
        if (r->kind == R_HEREDOC) {
            if (r->heredoc_quoted) {
                writeHeredoc(r->word);
                index++;
                continue;
            }
            waiting = true;
            return m.call(Expand::heredoc(r->word));
        }
        if (r->kind == R_DUP) {
            items.push_back(Item { SH_RD_DUP, r->fd, r->dupfd, -1, false });
            index++;
            continue;
        }
        if (r->kind == R_CLOSE) {
            items.push_back(Item { SH_RD_CLOSE, r->fd, 0, -1, false });
            index++;
            continue;
        }
        waiting = true;
        return m.call(Expand::single(r->word));
    }

    // Paths are only referenced once every word is expanded, so the vectors no longer grow.
    std::vector<sh_redir_rt> runtime;
    runtime.reserve(items.size());
    for (const auto& item : items) {
        const char* path = nullptr;
        if (item.path >= 0) {
            path = item.temp ? target->tempFiles[item.path].c_str() : target->paths[item.path].c_str();
        }
        runtime.push_back(sh_redir_rt { item.fd, item.op, path, item.dupfd });
    }
    if (sh_redir_apply(runtime.data(), static_cast<int>(runtime.size()), &target->io) != 0) {
        for (auto& path : target->tempFiles) remove(path.c_str());
        target->tempFiles.clear();
        return m.done(1);
    }
    target->active = true;
    return m.done(0);
}

void ApplyRedirects::writeHeredoc(const std::string& body)
{
    std::string path = temp_path(2);
    if (FILE* file = fopen(path.c_str(), "w")) {
        fwrite(body.data(), 1, body.size(), file);
        fclose(file);
    }
    target->tempFiles.push_back(std::move(path));
    items.push_back(Item { SH_RD_IN, 0, 0, static_cast<int>(target->tempFiles.size()) - 1, true });
}

// endregion

// region Command substitution

CommandSubst::~CommandSubst()
{
    if (redirected) sh_redir_restore(io.get());
    if (!tempFile.empty()) remove(tempFile.c_str());
}

// Run `command` as command substitution: execute it with stdout captured to a temp
// file, read the output back, and strip trailing newlines (POSIX). Control-flow
// flags are saved/restored so an inner `exit`/`break`/`continue` stays local to
// the substitution.
Step CommandSubst::step(Machine& m)
{
    if (!started) {
        started = true;
        tempFile = temp_path(2);
        savedFlow = FlowFlags::save(m.st);
        sh_redir_rt item = { 1, SH_RD_OUT, tempFile.c_str(), 0 };
        io = std::make_unique<sh_redir_saved>();
        if (sh_redir_apply(&item, 1, io.get()) != 0) {
            tempFile.clear();
            return finish(m, {});
        }
        redirected = true;
        return m.call(RunProgram(command, RunProgram::Mode::Plain));
    }

    sh_redir_restore(io.get());
    redirected = false;
    std::string output;
    if (FILE* file = fopen(tempFile.c_str(), "rb")) {
        int c;
        while ((c = fgetc(file)) != EOF) output.push_back(static_cast<char>(c));
        fclose(file);
    }
    remove(tempFile.c_str());
    tempFile.clear();
    return finish(m, std::move(output));
}

Step CommandSubst::finish(Machine& m, std::string output)
{
    sh_state& st = m.st;
    // Remember the substitution's own exit status: a command or assignment
    // whose only "command" is command substitution(s) reports the status of
    // the last one (POSIX). The program left it in last_status.
    st.cmdsub_ran = 1;
    st.cmdsub_status = st.last_status;
    savedFlow.restore(st);
    while (!output.empty() && output.back() == '\n') output.pop_back();
    return m.done(std::move(output));
}

// endregion

} // namespace sh

extern "C" {

int sh_run_string(sh_state* st, const char* src)
{
    sh::Machine machine(*st);
    return machine.run(sh::RunProgram(src, sh::RunProgram::Mode::Plain));
}

int sh_run_string_args(sh_state* st, const char* src, int argc, char** argv)
{
    if (argv) {
        const char* arg0 = argc > 0 ? argv[0] : nullptr;
        sh_set_positional(st, arg0, argv + 1, argc > 1 ? argc - 1 : 0);
    }
    return sh_run_string(st, src);
}

}
