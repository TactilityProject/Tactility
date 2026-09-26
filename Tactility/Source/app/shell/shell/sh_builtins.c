#include <Tactility/app/shell/shell/sh_builtins.h>
#include <Tactility/app/shell/shell/sh_port.h>
#include <Tactility/app/shell/ShellBridge.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>

static int is_int(const char *s, long *out)
{
    if (!s || !*s) return 0;
    char *end;
    long v = strtol(s, &end, 10);
    if (*end) return 0;
    *out = v;
    return 1;
}

// Is `name` (up to the given length) a valid shell variable name?
static int valid_varname_n(const char *name, size_t len)
{
    if (len == 0) return 0;
    if (!(name[0] == '_' || (name[0] >= 'a' && name[0] <= 'z') ||
          (name[0] >= 'A' && name[0] <= 'Z'))) return 0;
    for (size_t i = 1; i < len; i++) {
        char c = name[i];
        if (!(c == '_' || (c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return 0;
    }
    return 1;
}

// ---- test / [ --------------------------------------------------------------

static int file_test(char op, const char *path)
{
    if (op == 't') {                       // -t FD: is FD a terminal
        long fd;
        if (!is_int(path, &fd) || fd < 0 || fd > 1000000) return 1;
        return isatty((int)fd) ? 0 : 1;
    }
    /* Tactility: resolve against the shell's working directory, which the C library does not
     * share - otherwise `test -f out.txt` asks about /out.txt and is always false. */
    char resolved[256];
    if (path && shell_bridge_resolve(path, resolved, sizeof(resolved)) == 0) {
        path = resolved;
    }

    if (op == 'r' || op == 'w' || op == 'x') {
        int m = op == 'r' ? R_OK : op == 'w' ? W_OK : X_OK;
        return access(path, m) == 0 ? 0 : 1;
    }
    // No lstat on this platform, and the VFS has no symlinks anyway, so -h/-L
    // fall back to stat -- S_ISLNK is then always false, the correct answer.
    struct stat sb;
    if (stat(path, &sb) != 0)
        return 1;                          // false
    switch (op) {
        case 'e': return 0;
        case 'f': return S_ISREG(sb.st_mode) ? 0 : 1;
        case 'd': return S_ISDIR(sb.st_mode) ? 0 : 1;
        case 's': return sb.st_size > 0 ? 0 : 1;
        case 'h': case 'L': return S_ISLNK(sb.st_mode) ? 0 : 1;
    }
    return 1;
}

// ---- test expression parser (POSIX/dash grammar) ---------------------------
// Recursive-descent over the argv tokens, mirroring dash's test:
//   oexpr := aexpr { -o aexpr }        aexpr := nexpr { -a nexpr }
//   nexpr := ! nexpr | primary
//   primary := ( oexpr ) | <unop> arg | arg <binop> arg | arg
// Boolean helpers return 1 (true) / 0 (false); `t_err` flags a syntax error.

static int t_is_unop(const char *s)
{
    if (s[0] != '-' || s[1] == 0 || s[2] != 0) return 0;
    return strchr("znefdsrwxthL", s[1]) != NULL;
}

static int t_is_binop(const char *s)
{
    static const char *ops[] = { "=", "!=", "<", ">", "-eq", "-ne",
                                 "-lt", "-le", "-gt", "-ge", NULL };
    for (int i = 0; ops[i]; i++) if (strcmp(s, ops[i]) == 0) return 1;
    return 0;
}

static int t_unop(char op, const char *arg)
{
    if (op == 'z') return arg[0] == 0;
    if (op == 'n') return arg[0] != 0;
    return file_test(op, arg) == 0;            // file tests: 0(true) -> 1
}

static int t_binop(const char *a, const char *op, const char *b, int *err)
{
    if (strcmp(op, "=") == 0)  return strcmp(a, b) == 0;
    if (strcmp(op, "!=") == 0) return strcmp(a, b) != 0;
    if (strcmp(op, "<") == 0)  return strcmp(a, b) < 0;
    if (strcmp(op, ">") == 0)  return strcmp(a, b) > 0;
    long x, y;
    if (!is_int(a, &x) || !is_int(b, &y)) { *err = 1; return 0; }
    if (strcmp(op, "-eq") == 0) return x == y;
    if (strcmp(op, "-ne") == 0) return x != y;
    if (strcmp(op, "-lt") == 0) return x <  y;
    if (strcmp(op, "-le") == 0) return x <= y;
    if (strcmp(op, "-gt") == 0) return x >  y;
    if (strcmp(op, "-ge") == 0) return x >= y;
    *err = 1;
    return 0;
}

// Operator-precedence evaluation with explicit value/operator stacks, so a
// long run of `!` or `(` arguments can't exhaust the native stack. Operators:
// '!' (prefix, binds to the following primary or group), 'a' (-a) above 'o'
// (-o), and '(' as a group marker. Both sides of -a/-o are always evaluated,
// matching dash.
typedef struct {
    char *ops;
    int  *vals;
    int nops, nvals, cap;
} tstack;

static void t_push_op(tstack *t, char op) { t->ops[t->nops++] = op; }
static void t_push_val(tstack *t, int v)  { t->vals[t->nvals++] = v; }

// Apply the binary operator on top of the op stack.
static void t_reduce(tstack *t)
{
    char op = t->ops[--t->nops];
    int rhs = t->vals[--t->nvals];
    int lhs = t->vals[--t->nvals];
    t_push_val(t, op == 'a' ? (rhs && lhs) : (rhs || lhs));
}

// Push a finished operand, applying any '!' waiting directly in front of it.
static void t_push_operand(tstack *t, int v)
{
    while (t->nops > 0 && t->ops[t->nops - 1] == '!') { t->nops--; v = !v; }
    t_push_val(t, v);
}

// Evaluate argv[0..n). Returns 1/0 (true/false); sets *err on a syntax error;
// *consumed receives how many arguments were used.
static int t_evaluate(char **a, int n, int *err, int *consumed)
{
    tstack t;
    t.cap = n + 1;
    t.ops = malloc(t.cap);
    t.vals = malloc(t.cap * sizeof(int));
    t.nops = t.nvals = 0;

    int i = 0;
    int expect_operand = 1;
    for (;;) {
        if (expect_operand) {
            if (i >= n) { t_push_operand(&t, 0); expect_operand = 0; break; }   // missing expression -> false
            char *s = a[i];
            if (strcmp(s, "!") == 0) { t_push_op(&t, '!'); i++; continue; }
            if (strcmp(s, "(") == 0) { t_push_op(&t, '('); i++; continue; }
            int r;
            if (t_is_unop(s) && i + 1 < n) {
                r = t_unop(s[1], a[i + 1]);
                i += 2;
            } else if (i + 1 < n && t_is_binop(a[i + 1])) {
                if (i + 2 >= n) { *err = 1; i = n; r = 0; }
                else { r = t_binop(a[i], a[i + 1], a[i + 2], err); i += 3; }
            } else {
                r = s[0] != 0;
                i++;
            }
            t_push_operand(&t, r);
            expect_operand = 0;
            continue;
        }
        if (i < n && (strcmp(a[i], "-a") == 0 || strcmp(a[i], "-o") == 0)) {
            char op = a[i][1];
            while (t.nops > 0 && t.ops[t.nops - 1] != '(' && (op == 'o' || t.ops[t.nops - 1] == 'a')) t_reduce(&t);
            t_push_op(&t, op);
            i++;
            expect_operand = 1;
            continue;
        }
        if (i < n && strcmp(a[i], ")") == 0) {
            while (t.nops > 0 && t.ops[t.nops - 1] != '(') t_reduce(&t);
            if (t.nops == 0) break;   // unmatched ')': left for the caller to reject
            t.nops--;                 // the '('
            i++;
            int v = t.vals[--t.nvals];
            t_push_operand(&t, v);
            continue;
        }
        break;
    }
    while (t.nops > 0) {
        if (t.ops[t.nops - 1] == '(') { *err = 1; t.nops--; continue; }   // missing ')'
        if (t.ops[t.nops - 1] == '!') { t.nops--; continue; }
        t_reduce(&t);
    }
    int result = t.nvals > 0 ? t.vals[t.nvals - 1] : 0;
    free(t.ops);
    free(t.vals);
    *consumed = i;
    return result;
}

// Evaluate a test expression (argv/argc already stripped of the program name
// and, for '[', the trailing ']'). Returns 0 (true) / 1 (false) / 2 (error).
static int eval_test(int argc, char **argv)
{
    if (argc == 0) return 1;                       // empty -> false
    if (argc == 1) return argv[0][0] ? 0 : 1;      // non-empty string -> true

    // dash disambiguates the 3-arg form by the middle token: a binary operator
    // there wins over reading arg[0] as a unary operator (2-token lookahead).
    if (argc == 3 && t_is_binop(argv[1])) {
        int err = 0;
        int r = t_binop(argv[0], argv[1], argv[2], &err);
        return err ? 2 : (r ? 0 : 1);
    }

    int err = 0, consumed = 0;
    int r = t_evaluate(argv, argc, &err, &consumed);
    if (err || consumed != argc) return 2;
    return r ? 0 : 1;
}

static int builtin_test(int argc, char **argv, int bracket)
{
    // strip argv[0]; for '[' also require and strip trailing ']'
    int n = argc - 1;
    char **a = argv + 1;
    if (bracket) {
        if (n < 1 || strcmp(a[n - 1], "]") != 0) return 2;
        n--;
    }
    int r = eval_test(n, a);
    if (r == 2) { fprintf(stderr, "test: bad expression\n"); return 2; }
    return r;
}

// ---- echo ------------------------------------------------------------------

static int builtin_echo(int argc, char **argv)
{
    int start = 1;
    int newline = 1;
    if (start < argc && strcmp(argv[start], "-n") == 0) { newline = 0; start++; }
    // dash's echo is XSI: it always interprets backslash escapes (no -e needed).
    for (int i = start; i < argc; i++) {
        for (const char *s = argv[i]; *s; s++) {
            if (*s == '\\' && s[1]) {
                s++;
                switch (*s) {
                case 'a': fputc('\a', stdout); break;
                case 'b': fputc('\b', stdout); break;
                case 'c': return 0;   // \c: stop output, no trailing newline
                case 'f': fputc('\f', stdout); break;
                case 'n': fputc('\n', stdout); break;
                case 'r': fputc('\r', stdout); break;
                case 't': fputc('\t', stdout); break;
                case 'v': fputc('\v', stdout); break;
                case '\\': fputc('\\', stdout); break;
                case '0': {
                    int v = 0, k = 0;
                    while (k < 3 && s[1] >= '0' && s[1] <= '7') { v = v * 8 + (s[1] - '0'); s++; k++; }
                    fputc(v, stdout);
                    break;
                }
                default: fputc('\\', stdout); fputc(*s, stdout); break;
                }
            } else {
                fputc(*s, stdout);
            }
        }
        if (i < argc - 1) fputc(' ', stdout);
    }
    if (newline) fputc('\n', stdout);
    return 0;
}

// ---- read ------------------------------------------------------------------

// Field-split the raw line (buf[0..len), with a parallel `esc` flag marking
// backslash-escaped bytes that must stay literal) on $IFS, assigning up to
// `nv` fields to names[]. This mirrors dash's ifsbreakup: the first nv-1
// IFS-delimited fields are peeled off and the last name gets the remainder,
// with leading/trailing IFS whitespace stripped and IFS whitespace runs
// collapsed. Extra names get "".
static void read_split_assign(sh_state *st, const char *buf, const char *esc,
                              size_t len, char **names, int nv)
{
    const char *ifs = sh_get(st, "IFS");
    if (!ifs) ifs = " \t\n";

    // Collected field boundaries (at most nv of them).
    size_t fs[64], fe[64];
    int nf = 0;
    if (nv > 64) nv = 64;

    int maxargs = nv;
    size_t start = 0;
    int ifsspc = 0;
    long r = -1;   // start of trailing chars to drop (-1 = none)

    for (size_t i = 0; i < len; i++) {
        size_t q = i;
        char c = buf[i];
        int isifs = !esc[i] && c != 0 && strchr(ifs, c) != NULL;
        int isdefifs = isifs && (c == ' ' || c == '\t' || c == '\n');

        if (maxargs == 0) {
            // Remainder collector: track trailing IFS whitespace to drop.
            if (isdefifs) { if (r < 0) r = (long)q; continue; }
            if (!(isifs && ifsspc)) r = -1;
            ifsspc = 0;
            continue;
        }
        if (ifsspc) {
            if (isifs) q = i + 1;
            start = q;
            if (isdefifs) continue;
            isifs = 0;
        }
        if (isifs) {
            ifsspc = isdefifs;
            if (q == start && ifsspc) { start = i + 1; ifsspc = 0; continue; }
            maxargs--;
            if (maxargs == 0) { r = (long)q; continue; }
            if (nf < 64) { fs[nf] = start; fe[nf] = q; nf++; }
            start = i + 1;
            continue;
        }
        ifsspc = 0;
    }

    size_t end = (r >= 0) ? (size_t)r : len;
    if (start < end && nf < 64) { fs[nf] = start; fe[nf] = end; nf++; }

    for (int i = 0; i < nv; i++) {
        if (i < nf) {
            size_t n = fe[i] - fs[i];
            char *v = malloc(n + 1);
            memcpy(v, buf + fs[i], n);
            v[n] = 0;
            sh_set(st, names[i], v);
            free(v);
        } else {
            sh_set(st, names[i], "");
        }
    }
}

static int builtin_read(sh_state *st, int argc, char **argv)
{
    int rflag = 0;
    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == 0) break;   // not an option / bare "-"
        for (int j = 1; a[j]; j++) {
            if (a[j] == 'r') { rflag = 1; continue; }
            if (a[j] == 'p') {                  // -p prompt: consume its arg
                if (a[j + 1]) break;            // -pTEXT
                if (i + 1 < argc) i++;          // -p TEXT
                break;
            }
            fprintf(stderr, "read: illegal option -- %c\n", a[j]);
            return 2;
        }
    }

    char **names = argv + i;
    int nv = argc - i;
    if (nv <= 0) { fprintf(stderr, "read: arg count\n"); return 2; }

    // Read one logical line, byte by byte, honoring backslash escaping unless -r.
    size_t cap = 128, len = 0;
    char *buf = malloc(cap);
    char *esc = malloc(cap);
    int status = 0;
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (c == '\n') { status = 0; goto done; }
        if (c == '\0') continue;
        int e = 0;
        if (!rflag && c == '\\') {
            int nx = fgetc(stdin);
            if (nx == EOF) { status = 1; goto done; }   // trailing backslash dropped
            if (nx == '\n') continue;                    // line continuation
            c = nx;
            e = 1;
        }
        if (len + 1 > cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            esc = realloc(esc, cap);
        }
        buf[len] = (char)c;
        esc[len] = (char)e;
        len++;
    }
    status = 1;   // hit EOF without a terminating newline
done:
    read_split_assign(st, buf, esc, len, names, nv);
    free(buf);
    free(esc);
    return status;
}

// ----------------------------------------------------------------------------

int sh_run_builtin(sh_state *st, int argc, char **argv, int *status)
{
    if (argc == 0) return 0;
    const char *cmd = argv[0];

    if (strcmp(cmd, ":") == 0)     { *status = 0; return 1; }
    if (strcmp(cmd, "true") == 0)  { *status = 0; return 1; }
    if (strcmp(cmd, "false") == 0) { *status = 1; return 1; }

    if (strcmp(cmd, "echo") == 0)  { *status = builtin_echo(argc, argv); return 1; }

    if (strcmp(cmd, "test") == 0)  { *status = builtin_test(argc, argv, 0); return 1; }
    if (strcmp(cmd, "[") == 0)     { *status = builtin_test(argc, argv, 1); return 1; }

    if (strcmp(cmd, "exit") == 0) {
        long v = st->last_status;
        if (argc >= 2 && (!is_int(argv[1], &v) || v < 0)) {
            fprintf(stderr, "exit: Illegal number: %s\n", argv[1]);
            v = 2;
        }
        st->exiting = 1;
        st->exit_code = (int)v;
        *status = (int)v;
        return 1;
    }

    if (strcmp(cmd, "export") == 0) {
        // NAME=value assigns then exports; bare NAME marks (existing or future)
        // for export. Either way the value is mirrored into the environment.
        int rc = 0;
        for (int i = 1; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            size_t nlen = eq ? (size_t)(eq - argv[i]) : strlen(argv[i]);
            if (!valid_varname_n(argv[i], nlen)) {
                fprintf(stderr, "export: %s: bad variable name\n", argv[i]);
                rc = 2;
                continue;
            }
            if (eq) { *eq = 0; sh_set(st, argv[i], eq + 1); sh_export(st, argv[i]); *eq = '='; }
            else    { sh_export(st, argv[i]); }
        }
        *status = rc;
        return 1;
    }

    if (strcmp(cmd, "return") == 0) {
        long v = st->last_status;
        if (argc >= 2 && (!is_int(argv[1], &v) || v < 0)) {
            // A bad numeric argument to a special builtin is fatal in a
            // non-interactive shell (dash aborts with status 2). dash does NOT
            // truncate a return status; only exit() itself truncates.
            fprintf(stderr, "return: Illegal number: %s\n", argv[1]);
            st->exiting = 1; st->exit_code = 2;
            *status = 2;
            return 1;
        }
        st->returning = 1;
        st->return_code = (int)v;
        *status = (int)v;
        return 1;
    }

    if (strcmp(cmd, "local") == 0) {
        int rc = 0;
        for (int i = 1; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            size_t nlen = eq ? (size_t)(eq - argv[i]) : strlen(argv[i]);
            if (!valid_varname_n(argv[i], nlen)) {
                fprintf(stderr, "local: %s: bad variable name\n", argv[i]);
                rc = 1;
                continue;
            }
            if (eq) { *eq = 0; if (sh_local(st, argv[i], eq + 1)) rc = 1; *eq = '='; }
            else    { if (sh_local(st, argv[i], NULL)) rc = 1; }
        }
        if (rc) fprintf(stderr, "local: not in a function\n");
        *status = rc ? 2 : 0;
        return 1;
    }

    if (strcmp(cmd, "read") == 0) { *status = builtin_read(st, argc, argv); return 1; }

    if (strcmp(cmd, "break") == 0 || strcmp(cmd, "continue") == 0) {
        long n = 1;
        if (argc >= 2 && !is_int(argv[1], &n)) {
            // dash: a non-numeric count is an "Illegal number" error (status 2)
            // that still terminates the enclosing loop.
            fprintf(stderr, "%s: Illegal number: %s\n", cmd, argv[1]);
            if (st->loop_depth > 0) st->brk = 1;
            *status = 2;
            return 1;
        }
        if (n < 1) n = 1;
        // At the top level (no enclosing loop), dash treats break/continue as a
        // no-op with status 0 rather than an error.
        if (st->loop_depth > 0) {
            if (cmd[0] == 'b') st->brk = (int)n;
            else st->cont = (int)n;
        }
        *status = 0;
        return 1;
    }

    if (strcmp(cmd, "set") == 0) {
        // `set -- a b c` replaces the positional params. Bare `set` prints all
        // vars (name=value, sorted). Other flags are accepted but ignored here
        // (real set -e/-u/-x is out of scope for this tier).
        int i = 1;
        if (i >= argc) {
            // Collect and sort variable names.
            int n = 0;
            for (sh_var *v = st->vars; v; v = v->next) n++;
            const char **names = malloc((n ? n : 1) * sizeof(char *));
            n = 0;
            for (sh_var *v = st->vars; v; v = v->next) names[n++] = v->name;
            for (int a = 0; a < n; a++)
                for (int b = a + 1; b < n; b++)
                    if (strcmp(names[a], names[b]) > 0) {
                        const char *t = names[a]; names[a] = names[b]; names[b] = t;
                    }
            // dash prints `name='value'`, single-quoting the value (with the
            // '\'' idiom for embedded quotes) so the listing round-trips.
            for (int a = 0; a < n; a++) {
                printf("%s='", names[a]);
                for (const char *p = sh_get(st, names[a]); p && *p; p++) {
                    if (*p == '\'') printf("'\\''");
                    else putchar(*p);
                }
                printf("'\n");
            }
            free(names);
            *status = 0;
            return 1;
        }
        // Parse leading options. -e/-u (and their +forms) and -o/+o <errexit|
        // nounset> are real; other flags are accepted and ignored.
        // Positional params are replaced only if `--` is seen or operands remain
        // (so `set -u` alone leaves $@ untouched, `set -u --` clears it).
        int replace = 0;        // whether to replace positional params at the end
        for (; i < argc; i++) {
            char *a = argv[i];
            if (strcmp(a, "--") == 0) { i++; replace = 1; break; }
            if (a[0] != '-' && a[0] != '+') break;   // first operand
            if (a[1] == 0) {
                // Bare `-` stops option processing (like `--`) but only replaces
                // the params if operands actually follow. Bare `+` is an ignored
                // no-op flag; processing continues past it.
                if (a[0] == '-') { i++; break; }
                continue;
            }
            int on = (a[0] == '-');
            if (a[1] == 'o' && a[2] == 0) {          // -o NAME / +o NAME
                if (i + 1 < argc) {
                    i++;
                    if (strcmp(argv[i], "errexit") == 0) st->opt_errexit = on;
                    else if (strcmp(argv[i], "nounset") == 0) st->opt_nounset = on;
                    // other option names accepted but ignored
                }
                continue;
            }
            for (int k = 1; a[k]; k++) {              // short flag cluster
                if (a[k] == 'e') st->opt_errexit = on;
                else if (a[k] == 'u') st->opt_nounset = on;
                // -a/-x/etc. accepted but ignored
            }
        }
        // `--` always sets (clearing on empty); bare `-` and plain operands only
        // replace when something remains (dash leaves $@ untouched otherwise).
        if (replace || i < argc)
            sh_set_positional(st, NULL, argv + i, argc - i);
        *status = 0;
        return 1;
    }

    if (strcmp(cmd, "shift") == 0) {
        long nsh = 1;
        if (argc >= 2 && !is_int(argv[1], &nsh)) nsh = 1;
        if (sh_shift_positional(st, (int)nsh) != 0) {
            fprintf(stderr, "shift: can't shift that many\n");
            *status = 2;
        } else {
            *status = 0;
        }
        return 1;
    }

    if (strcmp(cmd, "unset") == 0) {
        int funcs = 0, vars = 0, rc = 0, i = 1;
        for (; i < argc; i++) {
            if (strcmp(argv[i], "-f") == 0) funcs = 1;
            else if (strcmp(argv[i], "-v") == 0) vars = 1;
            else break;
        }
        for (; i < argc; i++) {
            const char *nm = argv[i];
            if (funcs) { sh_func_unset(st, nm); continue; }
            // Reject invalid variable names (not [A-Za-z_][A-Za-z0-9_]*).
            int ok = isalpha((unsigned char)nm[0]) || nm[0] == '_';
            for (const char *p = nm + 1; ok && *p; p++)
                ok = isalnum((unsigned char)*p) || *p == '_';
            if (!ok) {
                // Bad identifier is a fatal usage error for this special builtin.
                fprintf(stderr, "unset: %s: bad variable name\n", nm);
                st->exiting = 1; st->exit_code = 2;
                *status = 2;
                return 1;
            }
            // Bare `unset NAME` removes a function only if no variable exists.
            if (!vars && !sh_get(st, nm) && sh_func_find(st, nm)) sh_func_unset(st, nm);
            else sh_unset(st, nm);
        }
        *status = rc;
        return 1;
    }

    if (strcmp(cmd, "cd") == 0) {
        const char *arg = argc >= 2 ? argv[1] : NULL;
        const char *target;
        int print = 0;

        if (!arg) {
            target = sh_get(st, "HOME");
            if (!target) { fprintf(stderr, "cd: HOME not set\n"); *status = 1; return 1; }
        } else if (strcmp(arg, "-") == 0) {
            target = sh_get(st, "OLDPWD");
            if (!target) { fprintf(stderr, "cd: OLDPWD not set\n"); *status = 1; return 1; }
            print = 1;
        } else {
            target = arg;
        }

        if (sh_port_chdir(target) != 0) {
            fprintf(stderr, "cd: %s: cannot change directory\n", target);
            *status = 2;   // dash returns 2 for a failed cd
            return 1;
        }

        // Track cwd physically (getcwd after chdir); logical-path fidelity
        // (symlink-preserving $PWD, lexical ..) is out of scope.
        char buf[512];
        sh_port_getcwd(buf, sizeof(buf));
        char *oldcwd = st->cwd;
        st->cwd = strdup(buf);
        sh_set(st, "OLDPWD", oldcwd ? oldcwd : "");
        sh_set(st, "PWD", buf);
        free(oldcwd);
        if (print) printf("%s\n", buf);
        *status = 0;
        return 1;
    }

    if (strcmp(cmd, "pwd") == 0) {
        int physical = 0;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-P") == 0) physical = 1;
            else if (strcmp(argv[i], "-L") == 0) physical = 0;
        }
        char buf[512];
        if (physical || !st->cwd) {
            sh_port_getcwd(buf, sizeof(buf));   // -P: resolve symlinks via the OS
            printf("%s\n", buf);
        } else {
            printf("%s\n", st->cwd);            // -L (default): the logical dir
        }
        *status = 0;
        return 1;
    }

    return 0;  // not a builtin
}
