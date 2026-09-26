// Word expansion: tilde, parameter, arithmetic and command substitution,
// quote removal, IFS field splitting and pathname globbing.
//
// Expansion is a pair of frames (see sh_machine.h). A word is scanned left to
// right by Expand, which suspends at `$(...)`, `${...}` and `$((...))` to let a
// child frame produce the value, then resumes scanning where it left off.
#include <Tactility/app/shell/shell/sh_machine.h>

#include <Tactility/app/shell/shell/sh_arith.h>
#include <Tactility/app/shell/shell/sh_glob.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {

void sh_fields_init(sh_fields* f) { f->items = nullptr; f->count = 0; f->cap = 0; }

void sh_fields_push(sh_fields* f, const char* s)
{
    if (f->count == f->cap) {
        f->cap = f->cap ? f->cap * 2 : 8;
        f->items = static_cast<char**>(realloc(f->items, f->cap * sizeof(char*)));
    }
    f->items[f->count++] = strdup(s);
}

void sh_fields_free(sh_fields* f)
{
    for (int i = 0; i < f->count; i++) free(f->items[i]);
    free(f->items);
    f->items = nullptr;
    f->count = f->cap = 0;
}

}

namespace sh {

namespace {

bool is_ifs(char c, const char* ifs) { return c && strchr(ifs, c) != nullptr; }
bool is_ifs_ws(char c) { return c == ' ' || c == '\t' || c == '\n'; }

// Does the raw (still-quoted) word contain a glob metachar outside quotes and
// not backslash-escaped? Only such words are candidates for pathname expansion:
// quoted metachars never glob, and (scope decision) metachars
// that arrive via $var/$() expansion don't either -- so the expanded fields can
// be used as patterns directly, with no quoted-mask tracking.
bool raw_has_glob(const std::string& raw)
{
    size_t n = raw.size();
    for (size_t i = 0; i < n; i++) {
        char c = raw[i];
        if (c == '\'') { i++; while (i < n && raw[i] != '\'') i++; if (i >= n) break; }
        else if (c == '"') { i++; while (i < n && raw[i] != '"') { if (raw[i] == '\\' && i + 1 < n) i++; i++; } if (i >= n) break; }
        else if (c == '\\') { if (i + 1 < n) i++; }
        else if (c == '*' || c == '?' || c == '[') return true;
    }
    return false;
}

} // namespace

// region Parameter helpers

const char* get_ifs(sh_state* st)
{
    const char* v = sh_get(st, "IFS");
    return v ? v : " \t\n";
}

// Append `v` to the field `b`. If `split`, break fields on $IFS following POSIX
// 2.6.5: leading IFS whitespace at the start of a field is elided; a run of IFS
// whitespace is one delimiter; each non-whitespace IFS char (with adjacent IFS
// whitespace) is also a delimiter and so can create empty fields between/at the
// front, but a lone trailing delimiter does not create a trailing empty field.
// The final (possibly partial or empty) field is left in `b` so it can continue
// across adjacent word parts; the caller flushes it with emit().
void append_val(FieldBuilder& b, Fields& out, const char* v, bool split, const char* ifs)
{
    if (!split) { for (const char* q = v; *q; q++) b.put(*q); return; }
    const char* q = v;
    // Strip leading IFS whitespace only when no earlier part has begun this field.
    if (!b.started)
        while (*q && is_ifs(*q, ifs) && is_ifs_ws(*q)) q++;
    for (;;) {
        // Accumulate the field's content up to the next IFS char.
        while (*q && !is_ifs(*q, ifs)) { b.put(*q); q++; }
        if (!*q) return;                 // field continues into next part / flush
        b.emitForced(out);               // delimiter -> field ends (even if empty)
        // Consume one delimiter: IFS whitespace, at most one non-ws IFS char,
        // then trailing IFS whitespace.
        while (*q && is_ifs(*q, ifs) && is_ifs_ws(*q)) q++;
        if (*q && is_ifs(*q, ifs) && !is_ifs_ws(*q)) {
            q++;
            while (*q && is_ifs(*q, ifs) && is_ifs_ws(*q)) q++;
        }
        if (!*q) return;                 // trailing delimiter: no empty field
    }
}

// Is `name` a plain variable name (assignable, not a positional/special param)?
bool is_var_name(const char* name)
{
    if (!(isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
    for (const char* q = name + 1; *q; q++)
        if (!(isalnum(static_cast<unsigned char>(*q)) || *q == '_')) return false;
    return true;
}

// Resolve a scalar parameter's raw value. Returns NULL if the parameter is
// unset (needed to distinguish unset from set-but-empty). *owned receives a
// malloc'd buffer the caller must free (may stay NULL).
const char* param_raw(sh_state* st, const char* name, char** owned)
{
    *owned = nullptr;
    if (!name[0]) return nullptr;
    if (name[1] == 0) {
        char c = name[0];
        if (c == '?') { auto* b = static_cast<char*>(malloc(16)); snprintf(b, 16, "%d", st->last_status); *owned = b; return b; }
        if (c == '#') { auto* b = static_cast<char*>(malloc(16)); snprintf(b, 16, "%d", st->npos); *owned = b; return b; }
        if (c == '$') return "1";   // stub: fake constant pid
        if (c == '!') return "";    // stub: no last bg pid, but "set"
        if (c == '-') return "";    // stub: shell flags
        if (c == '0') return st->arg0 ? st->arg0 : "";
    }
    bool alldig = true;
    for (const char* q = name; *q; q++)
        if (!isdigit(static_cast<unsigned char>(*q))) { alldig = false; break; }
    if (alldig) {
        long idx = strtol(name, nullptr, 10);
        if (idx == 0) return st->arg0 ? st->arg0 : "";
        if (idx >= 1 && idx <= st->npos) return st->pos[idx - 1];
        return nullptr;  // out of range -> unset
    }
    return sh_get(st, name);
}

// nounset (set -u): if `val` is unset (NULL) and nounset is on, report the error
// and abort the shell (dash exits 2). Returns true if it fired (caller emits nothing).
// Specials ($?, $#, $0, ...) resolve non-NULL, so only genuine unset vars and
// out-of-range positionals reach here; a bare $@/$* is handled separately.
bool nounset_fire(sh_state* st, const char* name, const char* val)
{
    if (val || !st->opt_nounset) return false;
    fprintf(stderr, "%s: parameter not set\n", name);
    st->exiting = 1; st->exit_code = 2; st->last_status = 2;
    return true;
}

// Expand $@ / $* into fields. `star` selects '*'. `quoted` is set inside "...".
void expand_at_star(sh_state* st, bool star, bool quoted, FieldBuilder& b, Fields& out, bool split, const char* ifs)
{
    int n = st->npos;
    char sep = ' ';
    const char* ifsv = sh_get(st, "IFS");
    if (ifsv) sep = ifsv[0];   // may be 0 if IFS=""

    if (star && quoted) {
        // "$*": join all params by the first char of $IFS into a single field.
        std::string joined;
        for (int i = 0; i < n; i++) {
            joined += st->pos[i];
            if (i < n - 1 && sep) joined.push_back(sep);
        }
        append_val(b, out, joined.c_str(), false, ifs);
        return;
    }

    // $@ : one field per positional param.
    if (n == 0) {
        // "$@" with no params vanishes; a non-empty prefix ("x$@") keeps the field.
        if (quoted && !star && b.text.empty()) b.started = false;
        return;
    }
    for (int i = 0; i < n; i++) {
        if (i > 0) b.emit(out);
        if (quoted) {
            for (char* q = st->pos[i]; *q; q++) b.put(*q);
            b.started = true;   // keep even empty params as fields
        } else {
            append_val(b, out, st->pos[i], split, ifs);
        }
    }
}

// endregion

// region Expand

Expand Expand::word(std::string raw)
{
    Expand e {};
    e.raw = std::move(raw);
    e.mode = Mode::Word;
    e.allowSplit = true;
    e.allowTilde = true;
    return e;
}

Expand Expand::single(std::string raw, bool allowTilde, bool quotedContext)
{
    Expand e {};
    e.raw = std::move(raw);
    e.mode = Mode::Single;
    e.allowTilde = allowTilde;
    e.quotedContext = quotedContext;
    return e;
}

// A here-doc body: parameter/command expansion and backslash escaping of
// $ ` \ (double-quote semantics), preserving newlines and literal quotes.
Expand Expand::heredoc(std::string raw)
{
    Expand e {};
    e.raw = std::move(raw);
    e.mode = Mode::Heredoc;
    return e;
}

Step Expand::step(Machine& m)
{
    if (pending != Pending::None) resume(m);

    if (!started) {
        started = true;
        if (mode == Mode::Heredoc) {
            field.started = true;   // an empty body is still a (zero-length) result
        } else if (at(0) == '~') {
            // Tilde expansion: only unquoted and only at word start. Since the tokenizer
            // keeps quotes in the raw word ("~" arrives as "\"~\""), a literal raw[0]=='~'
            // check gives the "unquoted, word-start" rule for free.
            size_t j = 1;
            while (at(j) && at(j) != '/') j++;
            if (allowTilde && j == 1) {  // bare "~" or "~/..." -> $HOME
                if (const char* home = sh_get(&m.st, "HOME")) {
                    for (const char* p = home; *p; p++) field.put(*p);
                    field.started = true;
                    i = j;  // resume at '/' or end; leaves ~name literal
                }
            }
            // ~user: no passwd db here, leave literal (handled by normal loop).
        }
    }

    bool suspended = (mode == Mode::Heredoc) ? scanHeredoc(m) : scanWord(m);
    return suspended ? Step::Call : finish(m);
}

// Handles the value a child frame produced for the expansion that suspended the scan.
void Expand::resume(Machine& m)
{
    Pending was = pending;
    pending = Pending::None;
    switch (was) {
        case Pending::Arithmetic: {
            // Command substitutions and `$var` forms inside the expression were
            // expanded first (parameter/command expansion only, no glob/split);
            // bare identifiers are left for the evaluator to dereference.
            long result = 0;
            const char* error = nullptr;
            if (sh_arith_eval(&m.st, m.result.text.c_str(), &result, &error) == 0) {
                char num[32];
                snprintf(num, sizeof(num), "%ld", result);
                append_val(field, out, num, pendingSplit, get_ifs(&m.st));
            } else {
                fprintf(stderr, "sh: arithmetic: %s\n", error ? error : "error");
                // dash treats an arithmetic evaluation error as a fatal error in a
                // non-interactive shell: abort the current command and exit 2.
                m.st.exiting = 1; m.st.exit_code = 2; m.st.last_status = 2;
            }
            break;
        }
        case Pending::CommandSubstitution:
            append_val(field, out, m.result.text.c_str(), pendingSplit, get_ifs(&m.st));
            break;
        case Pending::Backtick:
            for (char c : m.result.text) field.put(c);
            break;
        case Pending::Brace:
        case Pending::None:
            break;
    }
}

// Scans an ordinary word. Returns true when it suspended for a child frame.
bool Expand::scanWord(Machine& m)
{
    for (;;) {
        char c = at(i);
        if (inDoubleQuote) {
            if (c == '\0') { inDoubleQuote = false; continue; }
            if (c == '"') { i++; inDoubleQuote = false; continue; }
            char next = at(i + 1);
            if (c == '\\' && (next == '$' || next == '"' || next == '\\' || next == '`')) {
                field.put(next);
                i += 2;
            } else if (c == '$') {
                if (dollar(m, false, true)) return true;   // quoted: no split
            } else {
                field.put(c);
                i++;
            }
            continue;
        }

        if (c == '\0') return false;
        if (c == '\'') {
            field.started = true;
            i++;
            while (at(i) && at(i) != '\'') field.put(raw[i++]);
            if (at(i) == '\'') i++;
        } else if (c == '"') {
            field.started = true;
            i++;
            inDoubleQuote = true;
        } else if (c == '\\') {
            // In a double-quoted context (e.g. the word of `${x-...}` inside
            // "..."), a backslash only escapes $ " ` and itself; before any
            // other char it stays literal. Unquoted, it escapes the next char.
            if (quotedContext) {
                char next = at(i + 1);
                if (next == '$' || next == '"' || next == '`' || next == '\\') {
                    field.put(next);
                    i += 2;
                } else {
                    field.put('\\');
                    i++;
                }
            } else {
                i++;
                if (at(i)) field.put(raw[i++]);
            }
        } else if (c == '$') {
            if (dollar(m, allowSplit, false)) return true;
        } else {
            field.put(c);
            i++;
        }
    }
}

// Scans a here-doc body. Returns true when it suspended for a child frame.
bool Expand::scanHeredoc(Machine& m)
{
    while (at(i)) {
        char c = at(i);
        char next = at(i + 1);
        if (c == '\\' && (next == '$' || next == '`' || next == '\\' || next == '\n')) {
            if (next != '\n') field.put(next);   // a backslash-newline is a line continuation
            i += 2;
        } else if (c == '$') {
            if (dollar(m, false, true)) return true;   // quoted style, no split
        } else if (c == '`') {
            // Backtick command substitution reached without the lexer's
            // `->$() rewrite (e.g. inside a $((...)) operand). Capture to the
            // matching backtick and run it, honoring \` \\ \$ escapes.
            size_t j = i + 1;
            std::string command;
            while (at(j) && at(j) != '`') {
                if (at(j) == '\\' && (at(j + 1) == '`' || at(j + 1) == '\\' || at(j + 1) == '$')) {
                    command.push_back(at(j + 1));
                    j += 2;
                } else {
                    command.push_back(at(j));
                    j++;
                }
            }
            if (at(j) == '`') j++;
            i = j;
            pending = Pending::Backtick;
            m.call(CommandSubst(std::move(command)));
            return true;
        } else {
            field.put(c);
            i++;
        }
    }
    return false;
}

// Parse a $-expansion at raw[i] (raw[i]=='$') and append the value(s). `split`
// enables IFS word-splitting; `quoted` marks double-quote context. Returns true
// when the value comes from a child frame, which resume() then appends.
bool Expand::dollar(Machine& m, bool split, bool quoted)
{
    sh_state* st = &m.st;
    size_t p = i + 1;  // skip '$'

    if (at(p) == '(' && at(p + 1) == '(') {
        // $((expr)) arithmetic expansion. Scan to the matching `))`, tracking
        // nested `((`/`))` (so `$(( $((a)) + 1 ))` works).
        p += 2;
        size_t start = p;
        int depth = 1;
        while (at(p) && depth > 0) {
            if (at(p) == '(') depth++;
            else if (at(p) == ')') depth--;
            if (depth == 0) break;
            p++;
        }
        std::string inner = raw.substr(start, p - start);
        // Consume the closing `))`.
        if (at(p) == ')') p++;
        if (at(p) == ')') p++;
        i = p;
        pending = Pending::Arithmetic;
        pendingSplit = split;
        m.call(Expand::heredoc(std::move(inner)));
        return true;
    }

    if (at(p) == '(') {
        // $(...) command substitution.
        p++;
        size_t start = p;
        int depth = 1;
        while (at(p) && depth > 0) {
            char d = at(p);
            if (d == '\'') { p++; while (at(p) && at(p) != '\'') p++; if (at(p)) p++; continue; }
            if (d == '"') {
                p++;
                while (at(p) && at(p) != '"') { if (at(p) == '\\' && at(p + 1)) p += 2; else p++; }
                if (at(p)) p++;
                continue;
            }
            if (d == '\\' && at(p + 1)) { p += 2; continue; }
            if (d == '(') depth++;
            else if (d == ')') { if (--depth == 0) break; }
            p++;
        }
        std::string inner = raw.substr(start, p - start);
        if (at(p) == ')') p++;
        i = p;
        pending = Pending::CommandSubstitution;
        pendingSplit = quoted ? false : split;
        m.call(CommandSubst(std::move(inner)));
        return true;
    }

    if (at(p) == '{') {
        // ${...} : find the matching '}' (respect nesting, $(), quotes).
        p++;
        size_t start = p;
        int depth = 1;
        while (at(p) && depth > 0) {
            char d = at(p);
            if (d == '\\' && at(p + 1)) { p += 2; continue; }
            if (d == '\'') { p++; while (at(p) && at(p) != '\'') p++; if (at(p)) p++; continue; }
            if (d == '"') {
                p++;
                while (at(p) && at(p) != '"') { if (at(p) == '\\' && at(p + 1)) p += 2; else p++; }
                if (at(p)) p++;
                continue;
            }
            if (d == '$' && at(p + 1) == '{') { depth++; p += 2; continue; }
            if (d == '$' && at(p + 1) == '(') {
                p += 2;
                int pd = 1;
                while (at(p) && pd) { if (at(p) == '(') pd++; else if (at(p) == ')') { if (--pd == 0) { p++; break; } } p++; }
                continue;
            }
            if (d == '{') depth++;
            else if (d == '}') { if (--depth == 0) break; }
            p++;
        }
        std::string content = raw.substr(start, p - start);
        if (at(p) == '}') p++;
        i = p;
        pending = Pending::Brace;
        m.call(ExpandBrace { .content = std::move(content), .quoted = quoted, .split = split, .field = &field, .out = &out });
        return true;
    }

    const char* ifs = get_ifs(st);

    if (at(p) == '@' || at(p) == '*') {
        expand_at_star(st, at(p) == '*', quoted, field, out, split, ifs);
        i = p + 1;
        return false;
    }

    std::string name;
    char c = at(p);
    if (c == '?' || c == '#' || c == '$' || c == '!' || c == '-') {
        name.push_back(c);
        p++;
    } else if (isdigit(static_cast<unsigned char>(c))) {
        name.push_back(c);   // bare $N is a single digit
        p++;
    } else if (isalpha(static_cast<unsigned char>(c)) || c == '_') {
        while ((isalnum(static_cast<unsigned char>(at(p))) || at(p) == '_') && name.size() < 127) name.push_back(at(p++));
    } else {
        field.put('$');   // lone '$' - literal
        i++;
        return false;
    }

    char* owned = nullptr;
    const char* val = param_raw(st, name.c_str(), &owned);
    if (!nounset_fire(st, name.c_str(), val)) {
        append_val(field, out, val ? val : "", split, ifs);
    }
    free(owned);
    i = p;
    return false;
}

Step Expand::finish(Machine& m)
{
    if (mode == Mode::Heredoc) return m.done(std::move(field.text));

    field.emit(out);

    if (mode == Mode::Single) {
        if (out.empty()) return m.done(std::string());
        if (out.size() == 1) return m.done(std::move(out[0]));
        // join with spaces (shouldn't happen with split disabled)
        std::string joined;
        for (size_t k = 0; k < out.size(); k++) {
            if (k) joined.push_back(' ');
            joined += out[k];
        }
        return m.done(std::move(joined));
    }

    if (!raw_has_glob(raw)) return m.done(std::move(out));
    Fields globbed;
    for (auto& item : out) {
        sh_fields matches;
        sh_fields_init(&matches);
        if (sh_glob_pathnames(item.c_str(), &matches) == 0) {
            globbed.push_back(std::move(item));   // no match: keep literal
        } else {
            for (int k = 0; k < matches.count; k++) globbed.emplace_back(matches.items[k]);
        }
        sh_fields_free(&matches);
    }
    return m.done(std::move(globbed));
}

// endregion

// region ExpandBrace

Step ExpandBrace::step(Machine& m)
{
    sh_state* st = &m.st;
    const char* ifs = get_ifs(st);
    switch (phase) {
        case Phase::Start:
            return start(m);
        case Phase::Strip: {
            char* owned = nullptr;
            const char* v = param_raw(st, name.c_str(), &owned);
            const char* subject = v ? v : "";
            char* stripped = (stripKind == '#')
                ? sh_strip_prefix(subject, m.result.text.c_str(), longest)
                : sh_strip_suffix(subject, m.result.text.c_str(), longest);
            append_val(*field, *out, stripped, split, ifs);
            free(stripped);
            free(owned);
            break;
        }
        case Phase::Plus:
        case Phase::Minus:
            append_val(*field, *out, m.result.text.c_str(), quoted ? false : split, ifs);
            break;
        case Phase::Assign:
            if (is_var_name(name.c_str())) sh_set(st, name.c_str(), m.result.text.c_str());
            else fprintf(stderr, "%s: cannot assign in this way\n", name.c_str());
            append_val(*field, *out, m.result.text.c_str(), quoted ? false : split, get_ifs(st));
            break;
        case Phase::Error:
            if (!m.result.text.empty()) fprintf(stderr, "%s: %s\n", name.c_str(), m.result.text.c_str());
            else fprintf(stderr, "%s: parameter not set\n", name.c_str());
            st->exiting = 1;
            st->exit_code = 2;
            st->last_status = 2;
            break;
    }
    return m.done(0);
}

// Handle the interior of a ${...} expression (operators, length, plain).
Step ExpandBrace::start(Machine& m)
{
    sh_state* st = &m.st;
    const char* ifs = get_ifs(st);
    const char* c = content.c_str();

    // Length form: ${#name}
    if (c[0] == '#') {
        char d = c[1];
        if (isalnum(static_cast<unsigned char>(d)) || d == '_' || d == '@' || d == '*' ||
            (d == '#' && c[2] == 0)) {   // ${##}: length of "$#"
            const char* nm = c + 1;
            char num[16];
            if (strcmp(nm, "@") == 0 || strcmp(nm, "*") == 0) {
                snprintf(num, sizeof(num), "%d", st->npos);
            } else {
                char* owned = nullptr;
                const char* v = param_raw(st, nm, &owned);
                if (nounset_fire(st, nm, v)) { free(owned); return m.done(0); }
                snprintf(num, sizeof(num), "%d", v ? static_cast<int>(strlen(v)) : 0);
                free(owned);
            }
            append_val(*field, *out, num, split, ifs);
            return m.done(0);
        }
    }

    // Parse the parameter name.
    const char* r = c;
    if (*r == '@' || *r == '*' || *r == '?' || *r == '!' || *r == '$' || *r == '-' || *r == '#') {
        name.push_back(*r++);
    } else if (isdigit(static_cast<unsigned char>(*r))) {
        while (isdigit(static_cast<unsigned char>(*r)) && name.size() < 127) name.push_back(*r++);
    } else if (isalpha(static_cast<unsigned char>(*r)) || *r == '_') {
        while ((isalnum(static_cast<unsigned char>(*r)) || *r == '_') && name.size() < 127) name.push_back(*r++);
    }

    // No operator: plain ${name}.
    if (*r == 0) {
        if (name == "@" || name == "*") {
            expand_at_star(st, name[0] == '*', quoted, *field, *out, split, ifs);
        } else {
            char* owned = nullptr;
            const char* v = param_raw(st, name.c_str(), &owned);
            if (!nounset_fire(st, name.c_str(), v)) append_val(*field, *out, v ? v : "", split, ifs);
            free(owned);
        }
        return m.done(0);
    }

    // Prefix/suffix strip: ${v#p} ${v##p} ${v%p} ${v%%p}
    if (*r == '#' || *r == '%') {
        stripKind = *r++;
        if (*r == stripKind) { longest = true; r++; }
        phase = Phase::Strip;
        return m.call(Expand::single(r));
    }

    // Value operators: :- - :+ + := = :? ?
    bool colon = false;
    if (*r == ':' && (r[1] == '-' || r[1] == '+' || r[1] == '=' || r[1] == '?')) {
        colon = true;
        r++;
    }
    char op = *r;
    if (op == '-' || op == '+' || op == '=' || op == '?') {
        const char* word = r + 1;
        char* owned = nullptr;
        const char* val = param_raw(st, name.c_str(), &owned);
        bool set = val != nullptr;
        bool active = colon ? (!set || val[0] == 0) : !set;   // "empty or unset" test
        value = val ? val : "";
        free(owned);

        if (op == '+') {
            // Use word if the parameter IS set (non-empty for colon form).
            if (active) return m.done(0);
            phase = Phase::Plus;
        } else if (!active) {
            append_val(*field, *out, value.c_str(), quoted ? false : split, ifs);
            return m.done(0);
        } else {
            phase = (op == '-') ? Phase::Minus : (op == '=') ? Phase::Assign : Phase::Error;
        }
        return m.call(Expand::single(word, !quoted, quoted));
    }

    // Unknown operator: fall back to plain value.
    char* owned = nullptr;
    const char* v = param_raw(st, name.c_str(), &owned);
    append_val(*field, *out, v ? v : "", split, ifs);
    free(owned);
    return m.done(0);
}

// endregion

} // namespace sh
