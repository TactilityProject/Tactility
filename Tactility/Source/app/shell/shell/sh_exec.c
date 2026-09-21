#include <Tactility/app/shell/shell/sh.h>
#include <Tactility/app/shell/shell/sh_lex.h>
#include <Tactility/app/shell/shell/sh_parse.h>
#include <Tactility/app/shell/shell/sh_builtins.h>
#include <Tactility/app/shell/shell/sh_glob.h>
#include <Tactility/app/shell/shell/sh_port.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int exec_node(sh_state *st, node *n, int exempt);
static int call_function(sh_state *st, sh_func *fn, int argc, char **argv, int exempt);

// Run one already-expanded argv (function, builtin, or external), no redirection.
// `exempt` marks an errexit-suppressed context (propagated into functions).
static int run_argv(sh_state *st, int argc, char **argv, int exempt)
{
    int status = 0;
    // Functions override non-special builtins and externals (dash order).
    sh_func *fn = sh_func_find(st, argv[0]);
    if (fn) return call_function(st, fn, argc, argv, exempt);
    if (sh_run_builtin(st, argc, argv, &status)) return status;
    int found = 0;
    status = sh_port_run_external(argc, argv, &found);
    if (!found) {
        fprintf(stderr, "%s: not found\n", argv[0]);
        return 127;
    }
    return status;
}

// Apply a "NAME=raw" assignment (value is expanded). A leading `~` in the value
// tilde-expands via the normal word-start rule (so a=~/src works); the full
// POSIX colon rule (PATH=~/bin:~/sbin) is deliberately left out.
static void apply_assign(sh_state *st, const char *raw)
{
    const char *eq = strchr(raw, '=');
    if (!eq) return;
    int nlen = (int)(eq - raw);
    char name[128];
    if (nlen >= (int)sizeof(name)) nlen = sizeof(name) - 1;
    memcpy(name, raw, nlen);
    name[nlen] = 0;
    char *val = sh_expand_single(st, eq + 1);
    sh_set(st, name, val);
    free(val);
}

// Build argv by expanding all words. Returns argc; argv is malloc'd and
// NULL-terminated (caller frees each item and the array).
static int build_argv(sh_state *st, node *n, char ***out_argv)
{
    sh_fields f;
    sh_fields_init(&f);
    for (int i = 0; i < n->nword; i++) sh_expand_word(st, n->words[i], &f);
    char **argv = malloc((f.count + 1) * sizeof(char *));
    for (int i = 0; i < f.count; i++) argv[i] = f.items[i];  // take ownership
    argv[f.count] = NULL;
    free(f.items);  // items themselves transferred
    *out_argv = argv;
    return f.count;
}

static void free_argv(char **argv, int argc)
{
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
}

// Applied redirection context: the saved streams plus resources to release when
// the command finishes (expanded target strings and here-doc temp files).
typedef struct {
    sh_redir_saved io;
    int active;
    char *paths[16]; int npath;   // expanded filename strings to free
    char *tmps[8];   int ntmp;    // here-doc temp files to remove + free
} redir_state;

// Build the ordered runtime redirect list from a node and apply it. Returns 0
// on success (rs holds state for redir_end), -1 on failure (rs already cleaned;
// do not call redir_end).
static int redir_begin(sh_state *st, node *n, redir_state *rs)
{
    rs->active = 0; rs->npath = 0; rs->ntmp = 0;
    sh_redir_rt items[16];
    int ni = 0;
    for (int i = 0; i < n->nredir && ni < 16; i++) {
        sh_redir *r = &n->redirs[i];
        sh_redir_rt *it = &items[ni];
        it->fd = r->fd; it->dupfd = r->dupfd; it->path = NULL;
        if (r->kind == R_HEREDOC) {
            char *body = r->heredoc_quoted ? strdup(r->word)
                                           : sh_expand_heredoc(st, r->word);
            char buf[512];
            sh_port_tmpfile(2, buf, sizeof(buf));
            FILE *hf = fopen(buf, "w");
            if (hf) { fwrite(body, 1, strlen(body), hf); fclose(hf); }
            free(body);
            char *tmp = strdup(buf);
            if (rs->ntmp < 8) rs->tmps[rs->ntmp++] = tmp;
            it->op = SH_RD_IN; it->fd = 0; it->path = tmp;
        } else if (r->kind == R_DUP) {
            it->op = SH_RD_DUP;
        } else if (r->kind == R_CLOSE) {
            it->op = SH_RD_CLOSE;
        } else {
            char *path = sh_expand_single(st, r->word);
            if (rs->npath < 16) rs->paths[rs->npath++] = path;
            it->op = (r->kind == R_IN) ? SH_RD_IN
                   : (r->kind == R_APPEND) ? SH_RD_APPEND : SH_RD_OUT;
            it->path = path;
        }
        ni++;
    }
    if (sh_redir_apply(items, ni, &rs->io) != 0) {
        for (int i = 0; i < rs->npath; i++) free(rs->paths[i]);
        for (int i = 0; i < rs->ntmp; i++) { remove(rs->tmps[i]); free(rs->tmps[i]); }
        return -1;
    }
    rs->active = 1;
    return 0;
}

static void redir_end(redir_state *rs)
{
    if (rs->active) sh_redir_restore(&rs->io);
    for (int i = 0; i < rs->npath; i++) free(rs->paths[i]);
    for (int i = 0; i < rs->ntmp; i++) { remove(rs->tmps[i]); free(rs->tmps[i]); }
}

static int exec_simple(sh_state *st, node *n, int exempt)
{
    st->cmdsub_ran = 0;

    // Pure assignment (no command words): persist to shell state. If a value
    // contained command substitution, $? is that command sub's status; else 0.
    if (n->nword == 0) {
        for (int i = 0; i < n->nassign; i++) apply_assign(st, n->assigns[i]);
        if (st->exiting) return st->exit_code;   // ${a?msg} in a value is fatal
        if (n->nredir > 0) {
            // Bare redirect (`>file`): open/close the targets, run nothing.
            redir_state rs;
            if (redir_begin(st, n, &rs) != 0) return 2;
            redir_end(&rs);
        }
        return st->cmdsub_ran ? st->cmdsub_status : 0;
    }

    char **argv;
    int argc = build_argv(st, n, &argv);
    // A ${v:?} expansion may have aborted mid-word; don't run the command.
    if (st->exiting) {
        free_argv(argv, argc);
        return st->exit_code;
    }
    if (argc == 0) {
        // Command expanded to nothing (e.g. `$(exit 42)`); still apply pure
        // assignments. $? is the last command sub's status, or 0 if none ran.
        int cs = st->cmdsub_ran ? st->cmdsub_status : 0;
        for (int i = 0; i < n->nassign; i++) apply_assign(st, n->assigns[i]);
        free_argv(argv, argc);
        return cs;
    }

    // Temporary bindings: apply assignments, remember old values to restore.
    int nsaved = n->nassign;
    char *saved_names[32];
    char *saved_vals[32];
    char *saved_env[32];
    if (nsaved > 32) nsaved = 32;
    for (int i = 0; i < nsaved; i++) {
        const char *eq = strchr(n->assigns[i], '=');
        int nlen = (int)(eq - n->assigns[i]);
        char *nm = malloc(nlen + 1);
        memcpy(nm, n->assigns[i], nlen); nm[nlen] = 0;
        saved_names[i] = nm;
        const char *ov = sh_get(st, nm);
        saved_vals[i] = ov ? strdup(ov) : NULL;
        const char *oe = getenv(nm);
        saved_env[i] = oe ? strdup(oe) : NULL;
        apply_assign(st, n->assigns[i]);
        // A prefix binding is exported to the command's environment (POSIX);
        // setenv so the forked child inherits it. Restored below.
        const char *nv = sh_get(st, nm);
        setenv(nm, nv ? nv : "", 1);
    }

    // Redirections (ordered, fd-level).
    int status;
    redir_state rs;
    if (st->exiting) {   // fatal ${a?msg} while expanding a prefix binding
        free_argv(argv, argc);
        for (int i = 0; i < nsaved; i++) { free(saved_names[i]); free(saved_vals[i]); free(saved_env[i]); }
        return st->exit_code;
    }
    if (redir_begin(st, n, &rs) != 0) {
        fprintf(stderr, "%s: redirection failed\n", argv[0]);
        status = 2;   // dash exits a failed redirection with status 2
    } else {
        status = run_argv(st, argc, argv, exempt);
        redir_end(&rs);
    }

    // Restore temporary bindings.
    for (int i = 0; i < nsaved; i++) {
        if (saved_vals[i]) sh_set(st, saved_names[i], saved_vals[i]);
        else sh_unset(st, saved_names[i]);
        if (saved_env[i]) setenv(saved_names[i], saved_env[i], 1);
        else unsetenv(saved_names[i]);
        free(saved_names[i]);
        free(saved_vals[i]);
        free(saved_env[i]);
    }

    free_argv(argv, argc);
    return status;
}

static int exec_pipe(sh_state *st, node *n, int exempt)
{
    if (n->nchild == 1) return exec_node(st, n->children[0], exempt);

    // Pipelines are emulated with temp files. A stage may itself be a compound
    // command containing another pipeline (`{ a | b; } | c`, a pipe inside a
    // loop body, `eval` of a pipeline, ...), so each nesting level must use its
    // own temp files -- otherwise the inner pipeline clobbers and removes the
    // files backing the outer stage's redirection. Track depth and derive
    // distinct file ids from it.
    static int pipe_depth = 0;
    int d = pipe_depth++;

    char tmpa[512], tmpb[512];
    sh_port_tmpfile(2 * d, tmpa, sizeof(tmpa));
    sh_port_tmpfile(2 * d + 1, tmpb, sizeof(tmpb));

    int status = 0;
    const char *prev_in = NULL;
    for (int i = 0; i < n->nchild; i++) {
        const char *out = (i < n->nchild - 1) ? ((i % 2) ? tmpa : tmpb) : NULL;
        node *c = n->children[i];

        sh_redir_rt items[2];
        int ni = 0;
        if (prev_in) items[ni++] = (sh_redir_rt){ 0, SH_RD_IN, prev_in, 0 };
        if (out)     items[ni++] = (sh_redir_rt){ 1, SH_RD_OUT, out, 0 };
        sh_redir_saved io;
        if (sh_redir_apply(items, ni, &io) != 0) {
            status = 1;
            break;
        }
        // Each stage runs like a subshell for control flow: an inner `exit`,
        // `return`, or errexit abort stays local to the stage (real shells fork
        // each stage). Only the final stage's status becomes the pipeline's, and
        // errexit is decided at the pipeline level (see exec_node), so the stage
        // itself runs with errexit active (exempt inherited from the pipeline).
        int s_exit = st->exiting, s_code = st->exit_code;
        int s_brk = st->brk, s_cont = st->cont;
        int s_ret = st->returning, s_rcode = st->return_code;
        status = exec_node(st, c, exempt);
        st->exiting = s_exit; st->exit_code = s_code;
        st->brk = s_brk; st->cont = s_cont;
        st->returning = s_ret; st->return_code = s_rcode;
        sh_redir_restore(&io);

        prev_in = out;
    }
    remove(tmpa);
    remove(tmpb);
    pipe_depth--;
    return status;
}

static int exec_list(sh_state *st, node *n, int exempt)
{
    int status = 0;
    for (int i = 0; i < n->nchild; i++) {
        status = exec_node(st, n->children[i], exempt);
        if (st->exiting || st->returning || st->brk || st->cont) break;
    }
    return status;
}

static int exec_andor(sh_state *st, node *n, int exempt)
{
    // The left operand is always errexit-exempt (its failure is tested by the
    // &&/||). The right (tail) operand inherits the surrounding context.
    int status = exec_node(st, n->left, 1);
    if (st->exiting || st->returning) return status;
    if (n->andor_op == T_AMPAMP) {
        if (status == 0) status = exec_node(st, n->right, exempt);
    } else { // T_BARBAR
        if (status != 0) status = exec_node(st, n->right, exempt);
    }
    return status;
}

static int exec_if(sh_state *st, node *n, int exempt)
{
    for (int i = 0; i < n->nclause; i++) {
        int c = exec_node(st, n->conds[i], 1);   // condition: errexit-exempt
        if (st->exiting || st->returning) return c;
        if (c == 0) return exec_node(st, n->bodies[i], exempt);
    }
    if (n->else_body) return exec_node(st, n->else_body, exempt);
    return 0;
}

// Handle break/continue after running a loop body. Returns 1 if the loop
// should stop, 0 if it should continue to the next iteration.
static int loop_control(sh_state *st)
{
    if (st->exiting || st->returning) return 1;
    if (st->brk) { st->brk--; return 1; }        // break; outer loops see remaining
    if (st->cont) { st->cont--; return st->cont ? 1 : 0; }
    return 0;
}

static int exec_while(sh_state *st, node *n, int exempt)
{
    int status = 0;
    st->loop_depth++;
    while (!st->exiting) {
        int c = exec_node(st, n->cond, 1);   // condition: errexit-exempt
        // A break/continue evaluated inside the condition applies to this loop.
        if (st->brk || st->cont) { loop_control(st); break; }
        if (n->until) c = (c == 0);           // `until`: loop while cond fails
        if (st->exiting || st->returning || c != 0) break;
        status = exec_node(st, n->body, exempt);
        if (loop_control(st)) break;
    }
    st->loop_depth--;
    return status;
}

static int exec_for(sh_state *st, node *n, int exempt)
{
    int status = 0;
    sh_fields f;
    sh_fields_init(&f);
    if (n->for_implicit) {
        // `for x; do` iterates over "$@" (each positional param, unsplit).
        for (int i = 0; i < st->npos; i++) sh_fields_push(&f, st->pos[i]);
    } else {
        for (int i = 0; i < n->for_nword; i++) sh_expand_word(st, n->for_words[i], &f);
    }
    st->loop_depth++;
    for (int i = 0; i < f.count && !st->exiting; i++) {
        sh_set(st, n->for_name, f.items[i]);
        status = exec_node(st, n->body, exempt);
        if (loop_control(st)) break;
    }
    st->loop_depth--;
    sh_fields_free(&f);
    return status;
}

static int exec_case(sh_state *st, node *n, int exempt)
{
    char *subj = sh_expand_single(st, n->case_word);
    int status = 0;
    for (int i = 0; i < n->nclause_case; i++) {
        sh_case_clause *cl = &n->clauses[i];
        for (int j = 0; j < cl->npat; j++) {
            char *pat = sh_expand_single(st, cl->pats[j]);
            int hit = sh_pattern_match(pat, subj);
            free(pat);
            if (hit) {
                status = exec_node(st, cl->body, exempt);
                free(subj);
                return status;   // first match wins, no fallthrough
            }
        }
    }
    free(subj);
    return status;
}

// Deep-copy / free the variable list for subshell isolation.
static sh_var *clone_vars(sh_var *v)
{
    sh_var *head = NULL, **tail = &head;
    for (; v; v = v->next) {
        sh_var *c = malloc(sizeof(*c));
        c->name = strdup(v->name);
        c->value = strdup(v->value);
        c->next = NULL;
        *tail = c;
        tail = &c->next;
    }
    return head;
}

static void free_vars(sh_var *v)
{
    while (v) { sh_var *nx = v->next; free(v->name); free(v->value); free(v); v = nx; }
}

static int exec_group(sh_state *st, node *n, int exempt)
{
    if (!n->subshell) return exec_node(st, n->body, exempt);

    // Subshell ( list ): snapshot vars, positional params, cwd, and control
    // flags; run isolated; restore everything but the resulting $?.
    sh_var *saved_vars = clone_vars(st->vars);
    char **saved_pos = st->pos;
    int    saved_npos = st->npos;
    char  *saved_arg0 = st->arg0 ? strdup(st->arg0) : NULL;
    st->pos = NULL; st->npos = 0;
    if (saved_pos) {
        st->pos = malloc(saved_npos * sizeof(char *));
        for (int i = 0; i < saved_npos; i++) st->pos[i] = strdup(saved_pos[i]);
        st->npos = saved_npos;
    }
    char cwd[512];
    sh_port_getcwd(cwd, sizeof(cwd));
    int s_exit = st->exiting, s_code = st->exit_code;
    int s_brk = st->brk, s_cont = st->cont;
    int s_ret = st->returning, s_rcode = st->return_code;
    int s_ee = st->opt_errexit, s_nu = st->opt_nounset;

    int status = exec_node(st, n->body, exempt);

    // Restore. Option changes (set -e/-u) inside a subshell must not leak out.
    free_vars(st->vars);
    st->vars = saved_vars;
    for (int i = 0; i < st->npos; i++) free(st->pos[i]);
    free(st->pos);
    st->pos = saved_pos; st->npos = saved_npos;
    free(st->arg0); st->arg0 = saved_arg0;
    sh_port_chdir(cwd);
    st->exiting = s_exit; st->exit_code = s_code;
    st->brk = s_brk; st->cont = s_cont;
    st->returning = s_ret; st->return_code = s_rcode;
    st->opt_errexit = s_ee; st->opt_nounset = s_nu;
    return status;
}

static int exec_funcdef(sh_state *st, node *n)
{
    sh_func_define(st, n->func_name, n->body);
    n->body = NULL;   // ownership transferred; keep sh_free_node(root) from freeing it
    return 0;
}

// Call a function: swap in the call args as $1..$#, run the body under a fresh
// `local` scope, honor `return`, and restore the caller's positional params.
static int call_function(sh_state *st, sh_func *fn, int argc, char **argv, int exempt)
{
    if (st->call_depth >= SH_MAX_CALL_DEPTH) {
        fprintf(stderr, "%s: recursion too deep\n", fn->name);
        return 1;
    }
    char **saved_pos = st->pos;
    int    saved_npos = st->npos;
    st->pos = NULL; st->npos = 0;
    sh_set_positional(st, NULL, argv + 1, argc - 1);   // $0 unchanged (dash)

    sh_scope_push(st);
    st->call_depth++;
    int prev_ret = st->returning;
    st->returning = 0;

    exec_node(st, fn->body, exempt);

    int code = st->returning ? st->return_code : st->last_status;
    st->returning = prev_ret;
    st->call_depth--;
    sh_scope_pop(st);

    for (int i = 0; i < st->npos; i++) free(st->pos[i]);
    free(st->pos);
    st->pos = saved_pos; st->npos = saved_npos;
    return code;
}

static int exec_node(sh_state *st, node *n, int exempt)
{
    // Compound commands may carry a trailing redirect list; apply it around the
    // whole command. Simple commands handle their own redirects internally.
    redir_state rs;
    int has_redir = (n->kind != N_SIMPLE) && n->nredir > 0;
    if (has_redir && redir_begin(st, n, &rs) != 0) {
        st->last_status = 1;
        return 1;
    }

    // A negated pipeline (`! cmd`) is an errexit-exempt context throughout, and
    // its own non-zero result never triggers errexit.
    int inner_exempt = exempt || n->negated;

    int status = 0;
    switch (n->kind) {
        case N_LIST:    status = exec_list(st, n, inner_exempt); break;
        case N_ANDOR:   status = exec_andor(st, n, inner_exempt); break;
        case N_PIPE:    status = exec_pipe(st, n, inner_exempt); break;
        case N_SIMPLE:  status = exec_simple(st, n, inner_exempt); break;
        case N_IF:      status = exec_if(st, n, inner_exempt); break;
        case N_WHILE:   status = exec_while(st, n, inner_exempt); break;
        case N_FOR:     status = exec_for(st, n, inner_exempt); break;
        case N_CASE:    status = exec_case(st, n, inner_exempt); break;
        case N_GROUP:   status = exec_group(st, n, inner_exempt); break;
        case N_FUNCDEF: status = exec_funcdef(st, n); break;
    }
    if (has_redir) redir_end(&rs);

    if (n->negated) status = (status == 0);
    st->last_status = status;

    // errexit: a command that returns non-zero in a non-exempt context aborts.
    // Structural nodes (list / and-or / brace group) don't trigger — their inner
    // commands already decide — but a subshell `( )` is a command, so it does.
    // Setting `exiting` stops the current command list; a subshell/$() confines
    // it, the top level exits.
    int structural = (n->kind == N_LIST || n->kind == N_ANDOR ||
                      (n->kind == N_GROUP && !n->subshell));
    if (st->opt_errexit && !exempt && !n->negated && status != 0 && !structural &&
        !st->exiting && !st->returning && !st->brk && !st->cont) {
        st->exiting = 1;
        st->exit_code = status;
    }
    return status;
}

int sh_run_string_args(sh_state *st, const char *src, int argc, char **argv)
{
    if (argv) {
        const char *arg0 = argc > 0 ? argv[0] : NULL;
        sh_set_positional(st, arg0, argv + 1, argc > 1 ? argc - 1 : 0);
    }
    return sh_run_string(st, src);
}

int sh_run_string(sh_state *st, const char *src)
{
    st->parse_error = 0;
    sh_toklist tl;
    if (sh_lex(src, &tl) != 0) {
        fprintf(stderr, "sh: syntax error (unterminated quote)\n");
        st->parse_error = 1;
        st->last_status = 2;
        return 2;
    }
    const char *err = NULL;
    node *root = sh_parse(&tl, &err);
    if (!root) {
        fprintf(stderr, "sh: syntax error: %s\n", err ? err : "parse error");
        st->parse_error = 1;
        sh_toklist_free(&tl);
        st->last_status = 2;
        return 2;
    }
    int status = exec_node(st, root, 0);
    sh_free_node(root);
    sh_toklist_free(&tl);
    if (st->exiting) return st->exit_code;
    return status;
}
