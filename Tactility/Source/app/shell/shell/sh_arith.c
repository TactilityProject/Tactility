// POSIX arithmetic expansion `$(( ))` evaluator.
//
// Recursive-descent / precedence-climbing parser over a `const char *`,
// operating on signed `long` only. No floats, no arrays, no `**`/`++`/`--`
// (dash rejects those; so do we), and -- by scope decision -- no ternary,
// bitwise, or shift operators. Bare variable names auto-deref;
// unset/empty is 0; assignment operators write back to shell variables.
#include <Tactility/app/shell/shell/sh_arith.h>

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARITH_MAX_DEPTH 32   // recursion guard for variable-value re-eval

typedef struct {
    sh_state   *st;
    const char *p;        // current position
    int         err;      // nonzero once an error occurs
    const char *msg;      // error message (static string)
    int         depth;    // variable-deref recursion depth
} actx;

static long parse_comma(actx *c);
static long parse_assign(actx *c);

static void fail(actx *c, const char *m)
{
    if (!c->err) { c->err = 1; c->msg = m; }
}

static void skip_ws(actx *c)
{
    while (*c->p == ' ' || *c->p == '\t' || *c->p == '\n') c->p++;
}

// ---- token peeking ----------------------------------------------------------
// We tokenize on the fly. `peek` matches a fixed operator string at the current
// position (after skipping whitespace) without consuming.
static int peek(actx *c, const char *op)
{
    skip_ws(c);
    return strncmp(c->p, op, strlen(op)) == 0;
}

// Consume operator `op` if present; returns 1 if consumed.
static int eat(actx *c, const char *op)
{
    if (peek(c, op)) { c->p += strlen(op); return 1; }
    return 0;
}

// ---- variable deref ---------------------------------------------------------
// Evaluate the arithmetic value of a variable's string value (POSIX: variable
// values are themselves arithmetic expressions). Empty/unset -> 0.
static long deref(actx *c, const char *name)
{
    const char *v = sh_get(c->st, name);
    if (!v) return 0;
    while (*v == ' ' || *v == '\t' || *v == '\n') v++;  // empty/blank value -> 0
    if (!*v) return 0;
    if (c->depth >= ARITH_MAX_DEPTH) { fail(c, "arithmetic recursion too deep"); return 0; }
    // Parse the value as a fresh sub-expression.
    actx sub = *c;
    sub.p = v;
    sub.depth = c->depth + 1;
    long r = parse_comma(&sub);
    skip_ws(&sub);
    if (!sub.err && *sub.p) fail(&sub, "invalid arithmetic value");
    if (sub.err) { fail(c, sub.msg); return 0; }
    return r;
}

// Read a bare identifier at c->p into `name` (caller-sized buf). Returns len.
static int read_ident(actx *c, char *name, int cap)
{
    int n = 0;
    while ((isalnum((unsigned char)*c->p) || *c->p == '_') && n < cap - 1)
        name[n++] = *c->p++;
    name[n] = 0;
    return n;
}

// ---- integer literal --------------------------------------------------------
static long parse_number(actx *c)
{
    const char *s = c->p;
    long val = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        if (!isxdigit((unsigned char)*s)) { fail(c, "bad hex constant"); return 0; }
        while (isxdigit((unsigned char)*s)) {
            int d = isdigit((unsigned char)*s) ? *s - '0'
                    : (tolower((unsigned char)*s) - 'a' + 10);
            val = val * 16 + d;
            s++;
        }
    } else if (s[0] == '0' && isdigit((unsigned char)s[1])) {
        s++;                       // octal
        while (*s >= '0' && *s <= '7') { val = val * 8 + (*s - '0'); s++; }
        if (*s == '8' || *s == '9') { fail(c, "bad octal constant"); return 0; }
    } else {
        while (isdigit((unsigned char)*s)) { val = val * 10 + (*s - '0'); s++; }
    }
    c->p = s;
    return val;
}

// ---- primary ----------------------------------------------------------------
static long parse_primary(actx *c)
{
    skip_ws(c);
    if (*c->p == '(') {
        c->p++;
        long r = parse_comma(c);
        skip_ws(c);
        if (*c->p == ')') c->p++;
        else fail(c, "missing ) in arithmetic");
        return r;
    }
    // A leading '$' before an identifier: normal expansion would have handled
    // $var already, but if it reaches here, skip the '$' and deref the name.
    if (*c->p == '$') c->p++;

    if (isdigit((unsigned char)*c->p)) return parse_number(c);

    if (isalpha((unsigned char)*c->p) || *c->p == '_') {
        char name[128];
        read_ident(c, name, sizeof(name));
        skip_ws(c);
        // Assignment forms are handled at parse_assign level; here just deref.
        return deref(c, name);
    }
    fail(c, "unexpected token in arithmetic");
    return 0;
}

// ---- unary ------------------------------------------------------------------
static long parse_unary(actx *c)
{
    skip_ws(c);
    if (eat(c, "+")) return parse_unary(c);
    if (eat(c, "-")) return -parse_unary(c);
    if (eat(c, "!")) return !parse_unary(c);
    return parse_primary(c);
}

// ---- binary precedence levels ----------------------------------------------
static long parse_mul(actx *c)
{
    long l = parse_unary(c);
    for (;;) {
        skip_ws(c);
        // Reject `**` explicitly (not POSIX; dash errors).
        if (c->p[0] == '*' && c->p[1] == '*') { fail(c, "** not supported"); return 0; }
        if (eat(c, "*")) { l = l * parse_unary(c); }
        else if (eat(c, "/")) {
            long r = parse_unary(c);
            if (r == 0) { fail(c, "division by zero"); return 0; }
            l = l / r;
        } else if (eat(c, "%")) {
            long r = parse_unary(c);
            if (r == 0) { fail(c, "division by zero"); return 0; }
            l = l % r;
        } else break;
    }
    return l;
}

static long parse_add(actx *c)
{
    long l = parse_mul(c);
    for (;;) {
        skip_ws(c);
        // Guard against ++/-- (not POSIX).
        if ((c->p[0] == '+' && c->p[1] == '+') || (c->p[0] == '-' && c->p[1] == '-')) {
            fail(c, "++/-- not supported"); return 0;
        }
        if (eat(c, "+")) l = l + parse_mul(c);
        else if (eat(c, "-")) l = l - parse_mul(c);
        else break;
    }
    return l;
}

static long parse_rel(actx *c)
{
    long l = parse_add(c);
    for (;;) {
        skip_ws(c);
        if (eat(c, "<=")) l = (l <= parse_add(c));
        else if (eat(c, ">=")) l = (l >= parse_add(c));
        else if (eat(c, "<")) l = (l < parse_add(c));
        else if (eat(c, ">")) l = (l > parse_add(c));
        else break;
    }
    return l;
}

static long parse_eq(actx *c)
{
    long l = parse_rel(c);
    for (;;) {
        skip_ws(c);
        if (eat(c, "==")) l = (l == parse_rel(c));
        else if (eat(c, "!=")) l = (l != parse_rel(c));
        else break;
    }
    return l;
}

static long parse_land(actx *c)
{
    long l = parse_eq(c);
    while (eat(c, "&&")) { long r = parse_eq(c); l = (l && r); }
    return l;
}

static long parse_lor(actx *c)
{
    long l = parse_land(c);
    while (eat(c, "||")) { long r = parse_land(c); l = (l || r); }
    return l;
}

// ---- assignment (right-assoc) ----------------------------------------------
// Detect `<ident> <assign-op>` at the current position. On match, records the
// name and the compound operator char (0 for plain `=`), consumes both, and
// returns 1. Otherwise leaves position untouched and returns 0.
static int try_assign_lhs(actx *c, char *name, int cap, char *op)
{
    const char *save = c->p;
    skip_ws(c);
    if (*c->p == '$') c->p++;
    if (!(isalpha((unsigned char)*c->p) || *c->p == '_')) { c->p = save; return 0; }
    read_ident(c, name, cap);
    skip_ws(c);
    const char *ops[] = { "+=", "-=", "*=", "/=", "%=", NULL };
    for (int i = 0; ops[i]; i++) {
        size_t len = strlen(ops[i]);
        if (strncmp(c->p, ops[i], len) == 0) {
            *op = ops[i][len - 2];   // the operation char before '='
            c->p += len;
            return 1;
        }
    }
    // plain '=' but not '=='
    if (c->p[0] == '=' && c->p[1] != '=') { *op = 0; c->p++; return 1; }
    c->p = save;
    return 0;
}

static long apply_compound(actx *c, char op, long cur, long rhs)
{
    switch (op) {
        case 0:   return rhs;
        case '+': return cur + rhs;
        case '-': return cur - rhs;
        case '*': return cur * rhs;
        case '/': if (rhs == 0) { fail(c, "division by zero"); return 0; } return cur / rhs;
        case '%': if (rhs == 0) { fail(c, "division by zero"); return 0; } return cur % rhs;
        default:  return rhs;
    }
}

static long parse_assign(actx *c)
{
    char name[128], op;
    if (try_assign_lhs(c, name, sizeof(name), &op)) {
        long rhs = parse_assign(c);   // right-associative
        if (c->err) return 0;
        long cur = 0;
        if (op) cur = deref(c, name);
        long res = apply_compound(c, op, cur, rhs);
        if (c->err) return 0;
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", res);
        sh_set(c->st, name, buf);
        return res;
    }
    return parse_lor(c);
}

static long parse_comma(actx *c)
{
    long l = parse_assign(c);
    while (eat(c, ",")) l = parse_assign(c);
    return l;
}

int sh_arith_eval(sh_state *st, const char *expr, long *out, const char **errmsg)
{
    actx c = { .st = st, .p = expr, .err = 0, .msg = NULL, .depth = 0 };
    // An empty or all-whitespace expression evaluates to 0 (dash: `$(( ))`).
    skip_ws(&c);
    if (!*c.p) { if (errmsg) *errmsg = NULL; *out = 0; return 0; }
    long r = parse_comma(&c);
    skip_ws(&c);
    if (!c.err && *c.p) fail(&c, "unexpected trailing characters in arithmetic");
    if (c.err) {
        if (errmsg) *errmsg = c.msg;
        return 1;
    }
    if (errmsg) *errmsg = NULL;
    *out = r;
    return 0;
}
