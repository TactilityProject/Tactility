// BreezyBox shell scripting core - public API.
//
// Pure-C, no ESP-IDF includes. Everything platform-specific goes through
// sh_port.h.
#pragma once

#include <stdio.h>

// ---- shell state -----------------------------------------------------------

struct node;   // forward (AST body of a function)

typedef struct sh_var {
    char *name;
    char *value;
    int exported;   // marked for export: value is mirrored into the environment
    struct sh_var *next;
} sh_var;

// User-defined function: name + owned AST body.
typedef struct sh_func {
    char *name;
    struct node *body;
    struct sh_func *next;
} sh_func;

// One saved (name, prior value) record for a `local` binding.
typedef struct sh_saved_var {
    char *name;
    char *value;   // prior value (NULL if was unset)
    int   was_set;
    struct sh_saved_var *next;
} sh_saved_var;

// A per-function-call scope frame holding the vars `local` shadowed.
typedef struct sh_scope {
    sh_saved_var *saved;
    struct sh_scope *next;
} sh_scope;

#define SH_MAX_CALL_DEPTH 64

typedef struct {
    sh_var *vars;       // shell variables
    int last_status;    // $?
    int opt_errexit;    // set -e / -o errexit
    int opt_nounset;    // set -u / -o nounset
    int exiting;        // set by `exit`
    int exit_code;      // status to leave with
    int brk;            // pending `break`  levels (loop control)
    int cont;           // pending `continue` levels (loop control)
    int loop_depth;     // dynamic loop nesting; break/continue no-op at 0
    char **pos;         // positional params $1..$N  (pos[0] is $1)
    int    npos;        // $#
    char  *arg0;        // $0

    sh_func  *funcs;    // defined functions
    int       returning;   // set by `return`
    int       return_code; // status carried by `return`
    int       call_depth;  // active function-call nesting
    sh_scope *scopes;      // stack of `local` frames (top = current call)
    int       parse_error;  // last sh_run_string failed at lex/parse (not runtime)
    int       cmdsub_ran;   // a command substitution ran during current expansion
    int       cmdsub_status;// exit status of the last command substitution
    char     *cwd;         // logical current directory (dash-style, tracks $PWD
                           // without resolving symlinks); owned by the state
} sh_state;

void        sh_state_init(sh_state *st);
void        sh_state_free(sh_state *st);
const char *sh_get(sh_state *st, const char *name);   // NULL if unset
void        sh_set(sh_state *st, const char *name, const char *value);
void        sh_unset(sh_state *st, const char *name);
// Mark NAME for export (mirror into the environment now and on future sets).
void        sh_export(sh_state *st, const char *name);

// Replace the positional params ($1..$N) with copies of params[0..n-1].
// arg0 (may be NULL) sets $0; pass NULL to leave $0 unchanged.
void        sh_set_positional(sh_state *st, const char *arg0, char **params, int n);
// Drop the first n positional params. Returns 0 on success, 1 if n > $#.
int         sh_shift_positional(sh_state *st, int n);

// ---- functions & local scopes ----------------------------------------------

// Define (or redefine) a function, taking ownership of `body`.
void      sh_func_define(sh_state *st, const char *name, struct node *body);
sh_func  *sh_func_find(sh_state *st, const char *name);
int       sh_func_unset(sh_state *st, const char *name);  // returns 1 if removed

// Push/pop a `local` scope frame around a function call.
void sh_scope_push(sh_state *st);
void sh_scope_pop(sh_state *st);
// Record NAME's current value in the top frame, then set/unset it. Returns 0 on
// success, 1 if not inside a function (no frame).
int  sh_local(sh_state *st, const char *name, const char *value); // value NULL = unset

// ---- top level -------------------------------------------------------------

// Parse and execute an entire script buffer. Returns the exit status.
// Runs in the current state; does NOT touch positional params (so command
// substitution inherits $@ etc.).
int  sh_run_string(sh_state *st, const char *src);

// Like sh_run_string, but first installs positional params from argv:
// argv[0] = $0, argv[1..argc-1] = $1..$N. If argv is NULL, params are left
// untouched (this is how sh_run_string is defined).
int  sh_run_string_args(sh_state *st, const char *src, int argc, char **argv);

// ---- fields (expansion result) --------------------------------------------

typedef struct {
    char **items;
    int    count;
    int    cap;
} sh_fields;

void sh_fields_init(sh_fields *f);
void sh_fields_push(sh_fields *f, const char *s);
void sh_fields_free(sh_fields *f);

// Expand one raw word token into zero or more fields (word splitting applies to
// unquoted expansions). Appends to `out`.
void sh_expand_word(sh_state *st, const char *raw, sh_fields *out);
// Expand a raw word to a single string (no splitting) - for redirect targets
// and assignment values. Caller frees the returned string.
char *sh_expand_single(sh_state *st, const char *raw);
// Expand a here-doc body: parameter/command expansion and backslash escaping of
// $ ` \ (double-quote semantics), preserving newlines and literal quotes.
// Caller frees the returned string.
char *sh_expand_heredoc(sh_state *st, const char *raw);
