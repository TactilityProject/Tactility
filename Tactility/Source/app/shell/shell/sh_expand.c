#include <Tactility/app/shell/shell/sh.h>
#include <Tactility/app/shell/shell/sh_port.h>
#include <Tactility/app/shell/shell/sh_glob.h>
#include <Tactility/app/shell/shell/sh_arith.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void sh_fields_init(sh_fields *f) { f->items = NULL; f->count = 0; f->cap = 0; }

void sh_fields_push(sh_fields *f, const char *s)
{
    if (f->count == f->cap) {
        f->cap = f->cap ? f->cap * 2 : 8;
        f->items = realloc(f->items, f->cap * sizeof(char *));
    }
    f->items[f->count++] = strdup(s);
}

void sh_fields_free(sh_fields *f)
{
    for (int i = 0; i < f->count; i++) free(f->items[i]);
    free(f->items);
    f->items = NULL;
    f->count = f->cap = 0;
}

// A growable string used to build the current field.
typedef struct { char *buf; int len; int cap; int started; } fld;

static void fld_init(fld *b) { b->buf = malloc(16); b->cap = 16; b->len = 0; b->started = 0; b->buf[0] = 0; }
static void fld_put(fld *b, char c)
{
    if (b->len + 1 >= b->cap) { b->cap *= 2; b->buf = realloc(b->buf, b->cap); }
    b->buf[b->len++] = c;
    b->buf[b->len] = 0;
    b->started = 1;
}
static void fld_emit(fld *b, sh_fields *out)
{
    if (b->started) { sh_fields_push(out, b->buf); }
    b->len = 0; b->started = 0; b->buf[0] = 0;
}
// Like fld_emit, but always pushes a field even when empty/unstarted. Used at an
// IFS delimiter boundary, where a field ends regardless of whether it had any
// content (so `a__b` -> a,'',b rather than a,b).
static void fld_emit_force(fld *b, sh_fields *out)
{
    sh_fields_push(out, b->buf);
    b->len = 0; b->started = 0; b->buf[0] = 0;
}

// Run `cmd` as command substitution: execute it with stdout captured to a temp
// file, read the output back, and strip trailing newlines (POSIX). Control-flow
// flags are saved/restored so an inner `exit`/`break`/`continue` stays local to
// the substitution. Returns an owned string (never NULL).
static char *command_subst(sh_state *st, const char *cmd)
{
    static int depth = 0;   // nested $() get distinct temp names (avoid pipe 0/1)
    char tmp[512];
    sh_port_tmpfile(2 + depth, tmp, sizeof(tmp));
    depth++;

    int s_exit = st->exiting, s_code = st->exit_code;
    int s_brk = st->brk, s_cont = st->cont;
    int s_ret = st->returning, s_rcode = st->return_code;

    char *buf = NULL;
    int len = 0;
    sh_redir_rt item = { 1, SH_RD_OUT, tmp, 0 };
    sh_redir_saved io;
    if (sh_redir_apply(&item, 1, &io) == 0) {
        sh_run_string(st, cmd);
        sh_redir_restore(&io);
        FILE *f = fopen(tmp, "rb");
        if (f) {
            int cap = 0, c;
            while ((c = fgetc(f)) != EOF) {
                if (len + 1 >= cap) { cap = cap ? cap * 2 : 128; buf = realloc(buf, cap); }
                buf[len++] = (char)c;
            }
            fclose(f);
        }
        remove(tmp);
    }

    depth--;
    // Remember the substitution's own exit status: a command or assignment
    // whose only "command" is command substitution(s) reports the status of
    // the last one (POSIX). sh_run_string left it in last_status.
    st->cmdsub_ran = 1;
    st->cmdsub_status = st->last_status;
    st->exiting = s_exit; st->exit_code = s_code;
    st->brk = s_brk; st->cont = s_cont;
    st->returning = s_ret; st->return_code = s_rcode;

    if (!buf) return strdup("");
    while (len > 0 && buf[len - 1] == '\n') len--;
    char *res = realloc(buf, len + 1);
    if (res) buf = res;
    buf[len] = 0;
    return buf;
}

// ---- parameter helpers -----------------------------------------------------

static const char *get_ifs(sh_state *st)
{
    const char *v = sh_get(st, "IFS");
    return v ? v : " \t\n";
}
static int is_ifs(char c, const char *ifs) { return c && strchr(ifs, c) != NULL; }
static int is_ifs_ws(char c) { return c == ' ' || c == '\t' || c == '\n'; }

// Append `v` to the field `b`. If `split`, break fields on $IFS following POSIX
// 2.6.5: leading IFS whitespace at the start of a field is elided; a run of IFS
// whitespace is one delimiter; each non-whitespace IFS char (with adjacent IFS
// whitespace) is also a delimiter and so can create empty fields between/at the
// front, but a lone trailing delimiter does not create a trailing empty field.
// The final (possibly partial or empty) field is left in `b` so it can continue
// across adjacent word parts; the caller flushes it with fld_emit.
static void append_val(fld *b, sh_fields *out, const char *v, int split, const char *ifs)
{
    if (!split) { for (const char *q = v; *q; q++) fld_put(b, *q); return; }
    const char *q = v;
    // Strip leading IFS whitespace only when no earlier part has begun this field.
    if (!b->started)
        while (*q && is_ifs(*q, ifs) && is_ifs_ws(*q)) q++;
    for (;;) {
        // Accumulate the field's content up to the next IFS char.
        while (*q && !is_ifs(*q, ifs)) { fld_put(b, *q); q++; }
        if (!*q) return;                 // field continues into next part / flush
        fld_emit_force(b, out);          // delimiter -> field ends (even if empty)
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
static int is_var_name(const char *name)
{
    if (!(isalpha((unsigned char)name[0]) || name[0] == '_')) return 0;
    for (const char *q = name + 1; *q; q++)
        if (!(isalnum((unsigned char)*q) || *q == '_')) return 0;
    return 1;
}

// Resolve a scalar parameter's raw value. Returns NULL if the parameter is
// unset (needed to distinguish unset from set-but-empty). *owned receives a
// malloc'd buffer the caller must free (may stay NULL).
static const char *param_raw(sh_state *st, const char *name, char **owned)
{
    *owned = NULL;
    if (!name[0]) return NULL;
    if (name[1] == 0) {
        char c = name[0];
        if (c == '?') { char *b = malloc(16); snprintf(b, 16, "%d", st->last_status); *owned = b; return b; }
        if (c == '#') { char *b = malloc(16); snprintf(b, 16, "%d", st->npos); *owned = b; return b; }
        if (c == '$') return "1";   // stub: fake constant pid
        if (c == '!') return "";    // stub: no last bg pid, but "set"
        if (c == '-') return "";    // stub: shell flags
        if (c == '0') return st->arg0 ? st->arg0 : "";
    }
    int alldig = 1;
    for (const char *q = name; *q; q++)
        if (!isdigit((unsigned char)*q)) { alldig = 0; break; }
    if (alldig) {
        long idx = strtol(name, NULL, 10);
        if (idx == 0) return st->arg0 ? st->arg0 : "";
        if (idx >= 1 && idx <= st->npos) return st->pos[idx - 1];
        return NULL;  // out of range -> unset
    }
    return sh_get(st, name);
}

// nounset (set -u): if `val` is unset (NULL) and nounset is on, report the error
// and abort the shell (dash exits 2). Returns 1 if it fired (caller emits nothing).
// Specials ($?, $#, $0, ...) resolve non-NULL, so only genuine unset vars and
// out-of-range positionals reach here; a bare $@/$* is handled separately.
static int nounset_fire(sh_state *st, const char *name, const char *val)
{
    if (val || !st->opt_nounset) return 0;
    fprintf(stderr, "%s: parameter not set\n", name);
    st->exiting = 1; st->exit_code = 2; st->last_status = 2;
    return 1;
}

// Expand $@ / $* into fields. `star` selects '*'. `quoted` is set inside "...".
static void expand_at_star(sh_state *st, int star, int quoted, fld *b,
                           sh_fields *out, int split, const char *ifs)
{
    int n = st->npos;
    char sep = ' ';
    const char *ifsv = sh_get(st, "IFS");
    if (ifsv) sep = ifsv[0];   // may be 0 if IFS=""

    if (star && quoted) {
        // "$*": join all params by the first char of $IFS into a single field.
        int total = 1;
        for (int i = 0; i < n; i++) total += (int)strlen(st->pos[i]) + 1;
        char *tmp = malloc(total);
        tmp[0] = 0;
        int len = 0;
        for (int i = 0; i < n; i++) {
            int l = (int)strlen(st->pos[i]);
            memcpy(tmp + len, st->pos[i], l); len += l;
            if (i < n - 1 && sep) tmp[len++] = sep;
        }
        tmp[len] = 0;
        append_val(b, out, tmp, quoted ? 0 : split, ifs);
        free(tmp);
        return;
    }

    // $@ : one field per positional param.
    if (n == 0) return;   // "$@" with no params vanishes (prefix field kept)
    for (int i = 0; i < n; i++) {
        if (i > 0) fld_emit(b, out);
        if (quoted) {
            for (char *q = st->pos[i]; *q; q++) fld_put(b, *q);
            b->started = 1;   // keep even empty params as fields
        } else {
            append_val(b, out, st->pos[i], split, ifs);
        }
    }
}

static void do_brace(sh_state *st, const char *content, int quoted, fld *b,
                     sh_fields *out, int split, const char *ifs);
static char *sh_expand_single_t(sh_state *st, const char *raw, int allow_tilde,
                                int quoted_ctx);

// Parse a $-expansion at raw[*i] (raw[*i]=='$'). Append the value(s).
// `split` enables IFS word-splitting; `quoted` marks double-quote context.
static void do_dollar(sh_state *st, const char *raw, int *i, fld *b,
                      sh_fields *out, int split, int quoted)
{
    int p = *i + 1;  // skip '$'
    char name[128];
    int n = 0;
    const char *ifs = get_ifs(st);

    if (raw[p] == '(' && raw[p + 1] == '(') {
        // $((expr)) arithmetic expansion. Scan to the matching `))`, tracking
        // nested `((`/`))` (so `$(( $((a)) + 1 ))` works).
        p += 2;
        int start = p, depth = 1;
        while (raw[p] && depth > 0) {
            if (raw[p] == '(') depth++;
            else if (raw[p] == ')') depth--;
            if (depth == 0) break;
            p++;
        }
        int inner_len = p - start;
        char *inner = malloc(inner_len + 1);
        memcpy(inner, raw + start, inner_len);
        inner[inner_len] = 0;
        // Consume the closing `))`.
        if (raw[p] == ')') p++;
        if (raw[p] == ')') p++;
        // Command substitutions and `$var` forms inside the expression are
        // expanded first (parameter/command expansion only, no glob/split);
        // bare identifiers are left for the evaluator to dereference.
        char *expr = sh_expand_heredoc(st, inner);
        free(inner);
        inner = expr;
        long result = 0;
        const char *aerr = NULL;
        char num[32];
        if (sh_arith_eval(st, inner, &result, &aerr) == 0) {
            snprintf(num, sizeof(num), "%ld", result);
            append_val(b, out, num, split, ifs);
        } else {
            fprintf(stderr, "sh: arithmetic: %s\n", aerr ? aerr : "error");
            // dash treats an arithmetic evaluation error as a fatal error in a
            // non-interactive shell: abort the current command and exit 2.
            st->exiting = 1; st->exit_code = 2; st->last_status = 2;
        }
        free(inner);
        *i = p;
        return;
    }

    if (raw[p] == '(') {
        // $(...) command substitution.
        p++;
        int start = p, depth = 1;
        while (raw[p] && depth > 0) {
            char d = raw[p];
            if (d == '\'') { p++; while (raw[p] && raw[p] != '\'') p++; if (raw[p]) p++; continue; }
            if (d == '"') {
                p++;
                while (raw[p] && raw[p] != '"') { if (raw[p] == '\\' && raw[p + 1]) p += 2; else p++; }
                if (raw[p]) p++;
                continue;
            }
            if (d == '\\' && raw[p + 1]) { p += 2; continue; }
            if (d == '(') depth++;
            else if (d == ')') { if (--depth == 0) break; }
            p++;
        }
        int inner_len = p - start;
        char *inner = malloc(inner_len + 1);
        memcpy(inner, raw + start, inner_len);
        inner[inner_len] = 0;
        if (raw[p] == ')') p++;
        char *val = command_subst(st, inner);
        free(inner);
        append_val(b, out, val, quoted ? 0 : split, ifs);
        free(val);
        *i = p;
        return;
    }

    if (raw[p] == '{') {
        // ${...} : find the matching '}' (respect nesting, $(), quotes).
        p++;
        int start = p, depth = 1;
        while (raw[p] && depth > 0) {
            char d = raw[p];
            if (d == '\\' && raw[p + 1]) { p += 2; continue; }
            if (d == '\'') { p++; while (raw[p] && raw[p] != '\'') p++; if (raw[p]) p++; continue; }
            if (d == '"') {
                p++;
                while (raw[p] && raw[p] != '"') { if (raw[p] == '\\' && raw[p + 1]) p += 2; else p++; }
                if (raw[p]) p++;
                continue;
            }
            if (d == '$' && raw[p + 1] == '{') { depth++; p += 2; continue; }
            if (d == '$' && raw[p + 1] == '(') {
                p += 2; int pd = 1;
                while (raw[p] && pd) { if (raw[p] == '(') pd++; else if (raw[p] == ')') { if (--pd == 0) { p++; break; } } p++; }
                continue;
            }
            if (d == '{') depth++;
            else if (d == '}') { if (--depth == 0) break; }
            p++;
        }
        int clen = p - start;
        char *content = malloc(clen + 1);
        memcpy(content, raw + start, clen);
        content[clen] = 0;
        if (raw[p] == '}') p++;
        do_brace(st, content, quoted, b, out, split, ifs);
        free(content);
        *i = p;
        return;
    }

    if (raw[p] == '@' || raw[p] == '*') {
        expand_at_star(st, raw[p] == '*', quoted, b, out, split, ifs);
        *i = p + 1;
        return;
    }

    if (raw[p] == '?' || raw[p] == '#' || raw[p] == '$' || raw[p] == '!' || raw[p] == '-') {
        name[0] = raw[p]; name[1] = 0; p++;
    } else if (isdigit((unsigned char)raw[p])) {
        name[0] = raw[p]; name[1] = 0; p++;   // bare $N is a single digit
    } else if (isalpha((unsigned char)raw[p]) || raw[p] == '_') {
        while ((isalnum((unsigned char)raw[p]) || raw[p] == '_') && n < (int)sizeof(name) - 1)
            name[n++] = raw[p++];
        name[n] = 0;
    } else {
        fld_put(b, '$');   // lone '$' - literal
        *i = *i + 1;
        return;
    }

    char *owned = NULL;
    const char *val = param_raw(st, name, &owned);
    if (nounset_fire(st, name, val)) { free(owned); *i = p; return; }
    append_val(b, out, val ? val : "", split, ifs);
    free(owned);
    *i = p;
}

// Handle the interior of a ${...} expression (operators, length, plain).
static void do_brace(sh_state *st, const char *content, int quoted, fld *b,
                     sh_fields *out, int split, const char *ifs)
{
    // Length form: ${#name}
    if (content[0] == '#') {
        char d = content[1];
        if (isalnum((unsigned char)d) || d == '_' || d == '@' || d == '*' ||
            (d == '#' && content[2] == 0)) {   // ${##}: length of "$#"
            const char *nm = content + 1;
            char num[16];
            if (strcmp(nm, "@") == 0 || strcmp(nm, "*") == 0) {
                snprintf(num, sizeof(num), "%d", st->npos);
            } else {
                char *ow = NULL;
                const char *v = param_raw(st, nm, &ow);
                if (nounset_fire(st, nm, v)) { free(ow); return; }
                snprintf(num, sizeof(num), "%d", v ? (int)strlen(v) : 0);
                free(ow);
            }
            append_val(b, out, num, split, ifs);
            return;
        }
    }

    // Parse the parameter name.
    char name[128];
    int nl = 0;
    const char *r = content;
    if (*r == '@' || *r == '*' || *r == '?' || *r == '!' || *r == '$' ||
        *r == '-' || *r == '#') {
        name[0] = *r; nl = 1; r++;
    } else if (isdigit((unsigned char)*r)) {
        while (isdigit((unsigned char)*r) && nl < 127) name[nl++] = *r++;
    } else if (isalpha((unsigned char)*r) || *r == '_') {
        while ((isalnum((unsigned char)*r) || *r == '_') && nl < 127) name[nl++] = *r++;
    }
    name[nl] = 0;

    // No operator: plain ${name}.
    if (*r == 0) {
        if (strcmp(name, "@") == 0 || strcmp(name, "*") == 0) {
            expand_at_star(st, name[0] == '*', quoted, b, out, split, ifs);
        } else {
            char *ow = NULL;
            const char *v = param_raw(st, name, &ow);
            if (nounset_fire(st, name, v)) { free(ow); return; }
            append_val(b, out, v ? v : "", split, ifs);
            free(ow);
        }
        return;
    }

    // Prefix/suffix strip: ${v#p} ${v##p} ${v%p} ${v%%p}
    if (*r == '#' || *r == '%') {
        char kind = *r; r++;
        int longest = 0;
        if (*r == kind) { longest = 1; r++; }
        char *pat = sh_expand_single(st, r);
        char *ow = NULL;
        const char *v = param_raw(st, name, &ow);
        const char *sv = v ? v : "";
        char *res = (kind == '#') ? sh_strip_prefix(sv, pat, longest)
                                  : sh_strip_suffix(sv, pat, longest);
        append_val(b, out, res, split, ifs);
        free(res); free(pat); free(ow);
        return;
    }

    // Value operators: :- - :+ + := = :? ?
    int colon = 0;
    if (*r == ':' && (r[1] == '-' || r[1] == '+' || r[1] == '=' || r[1] == '?')) {
        colon = 1; r++;
    }
    char op = *r;
    if (op == '-' || op == '+' || op == '=' || op == '?') {
        const char *word = r + 1;
        char *ow = NULL;
        const char *val = param_raw(st, name, &ow);
        int set = (val != NULL);
        int active = colon ? (!set || val[0] == 0) : !set;  // "empty or unset" test

        if (op == '+') {
            // Use word if the parameter IS set (non-empty for colon form).
            if (!active) {
                char *w = sh_expand_single_t(st, word, !quoted, quoted);
                append_val(b, out, w, quoted ? 0 : split, ifs);
                free(w);
            }
        } else if (op == '-') {
            if (active) {
                char *w = sh_expand_single_t(st, word, !quoted, quoted);
                append_val(b, out, w, quoted ? 0 : split, ifs);
                free(w);
            } else {
                append_val(b, out, val, quoted ? 0 : split, ifs);
            }
        } else if (op == '=') {
            if (active) {
                char *w = sh_expand_single_t(st, word, !quoted, quoted);
                if (is_var_name(name)) sh_set(st, name, w);
                else fprintf(stderr, "%s: cannot assign in this way\n", name);
                append_val(b, out, w, quoted ? 0 : split, ifs);
                free(w);
            } else {
                append_val(b, out, val, quoted ? 0 : split, ifs);
            }
        } else { // '?'
            if (active) {
                char *w = sh_expand_single_t(st, word, !quoted, quoted);
                if (w[0]) fprintf(stderr, "%s: %s\n", name, w);
                else fprintf(stderr, "%s: parameter not set\n", name);
                free(w);
                st->exiting = 1;
                st->exit_code = 2;
                st->last_status = 2;
            } else {
                append_val(b, out, val, quoted ? 0 : split, ifs);
            }
        }
        free(ow);
        return;
    }

    // Unknown operator: fall back to plain value.
    char *ow = NULL;
    const char *v = param_raw(st, name, &ow);
    append_val(b, out, v ? v : "", split, ifs);
    free(ow);
}

// Core expansion of a raw word. Emits one or more fields into `out`.
static void expand(sh_state *st, const char *raw, sh_fields *out,
                   int allow_split, int allow_tilde, int quoted_ctx)
{
    fld b;
    fld_init(&b);
    int i = 0;

    // Tilde expansion: only unquoted and only at word start. Since the tokenizer
    // keeps quotes in the raw word ("~" arrives as "\"~\""), a literal raw[0]=='~'
    // check gives the "unquoted, word-start" rule for free.
    if (raw[0] == '~') {
        int j = 1;
        while (raw[j] && raw[j] != '/') j++;
        if (allow_tilde && j == 1) {  // bare "~" or "~/..." -> $HOME
            const char *home = sh_get(st, "HOME");
            if (home) {
                for (const char *p = home; *p; p++) fld_put(&b, *p);
                b.started = 1;
                i = j;  // resume at '/' or end; leaves ~name literal
            }
        }
        // ~user: no passwd db here, leave literal (handled by normal loop).
    }

    while (raw[i]) {
        char c = raw[i];
        if (c == '\'') {
            b.started = 1;
            i++;
            while (raw[i] && raw[i] != '\'') fld_put(&b, raw[i++]);
            if (raw[i] == '\'') i++;
        } else if (c == '"') {
            b.started = 1;
            i++;
            while (raw[i] && raw[i] != '"') {
                if (raw[i] == '\\' && (raw[i+1] == '$' || raw[i+1] == '"' ||
                                       raw[i+1] == '\\' || raw[i+1] == '`')) {
                    fld_put(&b, raw[i+1]); i += 2;
                } else if (raw[i] == '$') {
                    do_dollar(st, raw, &i, &b, out, 0, 1);  // quoted: no split
                } else {
                    fld_put(&b, raw[i++]);
                }
            }
            if (raw[i] == '"') i++;
        } else if (c == '\\') {
            // In a double-quoted context (e.g. the word of `${x-...}` inside
            // "..."), a backslash only escapes $ " ` and itself; before any
            // other char it stays literal. Unquoted, it escapes the next char.
            if (quoted_ctx) {
                char nx = raw[i+1];
                if (nx == '$' || nx == '"' || nx == '`' || nx == '\\') {
                    fld_put(&b, nx); i += 2;
                } else {
                    fld_put(&b, '\\'); i++;
                }
            } else {
                i++;
                if (raw[i]) fld_put(&b, raw[i++]);
            }
        } else if (c == '$') {
            do_dollar(st, raw, &i, &b, out, allow_split, 0);
        } else {
            fld_put(&b, c);
            i++;
        }
    }
    fld_emit(&b, out);
    free(b.buf);
}

// Does the raw (still-quoted) word contain a glob metachar outside quotes and
// not backslash-escaped? Only such words are candidates for pathname expansion:
// quoted metachars never glob, and (scope decision) metachars
// that arrive via $var/$() expansion don't either -- so the expanded fields can
// be used as patterns directly, with no quoted-mask tracking.
static int raw_has_glob(const char *raw)
{
    for (int i = 0; raw[i]; i++) {
        char c = raw[i];
        if (c == '\'') { i++; while (raw[i] && raw[i] != '\'') i++; if (!raw[i]) break; }
        else if (c == '"') { i++; while (raw[i] && raw[i] != '"') { if (raw[i] == '\\' && raw[i+1]) i++; i++; } if (!raw[i]) break; }
        else if (c == '\\') { if (raw[i+1]) i++; }
        else if (c == '*' || c == '?' || c == '[') return 1;
    }
    return 0;
}

void sh_expand_word(sh_state *st, const char *raw, sh_fields *out)
{
    if (!raw_has_glob(raw)) {
        expand(st, raw, out, 1, 1, 0);
        return;
    }
    sh_fields tmp;
    sh_fields_init(&tmp);
    expand(st, raw, &tmp, 1, 1, 0);
    for (int i = 0; i < tmp.count; i++) {
        if (sh_glob_pathnames(tmp.items[i], out) == 0)
            sh_fields_push(out, tmp.items[i]);   // no match: keep literal
    }
    sh_fields_free(&tmp);
}

char *sh_expand_heredoc(sh_state *st, const char *raw)
{
    sh_fields f;
    sh_fields_init(&f);   // do_dollar needs an out sink; splitting is disabled
    fld b;
    fld_init(&b);
    b.started = 1;        // an empty body is still a (zero-length) result
    int i = 0;
    while (raw[i]) {
        char c = raw[i];
        if (c == '\\' && (raw[i+1] == '$' || raw[i+1] == '`' ||
                          raw[i+1] == '\\' || raw[i+1] == '\n')) {
            if (raw[i+1] == '\n') i += 2;              // line continuation
            else { fld_put(&b, raw[i+1]); i += 2; }
        } else if (c == '$') {
            do_dollar(st, raw, &i, &b, &f, 0, 1);      // quoted style, no split
        } else if (c == '`') {
            // Backtick command substitution reached without the lexer's
            // `->$() rewrite (e.g. inside a $((...)) operand). Capture to the
            // matching backtick and run it, honoring \` \\ \$ escapes.
            int j = i + 1;
            fld cmd; fld_init(&cmd); cmd.started = 1;
            while (raw[j] && raw[j] != '`') {
                if (raw[j] == '\\' && (raw[j+1] == '`' || raw[j+1] == '\\' ||
                                       raw[j+1] == '$')) {
                    fld_put(&cmd, raw[j+1]); j += 2;
                } else {
                    fld_put(&cmd, raw[j]); j++;
                }
            }
            if (raw[j] == '`') j++;
            char *val = command_subst(st, cmd.buf);
            for (const char *p = val; *p; p++) fld_put(&b, *p);
            free(val);
            free(cmd.buf);
            i = j;
        } else {
            fld_put(&b, c); i++;
        }
    }
    char *res = strdup(b.buf);
    free(b.buf);
    sh_fields_free(&f);
    return res;
}

char *sh_expand_single(sh_state *st, const char *raw)
{
    return sh_expand_single_t(st, raw, 1, 0);
}

static char *sh_expand_single_t(sh_state *st, const char *raw, int allow_tilde,
                                int quoted_ctx)
{
    sh_fields f;
    sh_fields_init(&f);
    expand(st, raw, &f, 0, allow_tilde, quoted_ctx);
    char *res;
    if (f.count == 0) res = strdup("");
    else if (f.count == 1) res = strdup(f.items[0]);
    else {
        // join with spaces (shouldn't happen with split disabled)
        int len = 0;
        for (int i = 0; i < f.count; i++) len += strlen(f.items[i]) + 1;
        res = malloc(len + 1);
        res[0] = 0;
        for (int i = 0; i < f.count; i++) { if (i) strcat(res, " "); strcat(res, f.items[i]); }
    }
    sh_fields_free(&f);
    return res;
}
