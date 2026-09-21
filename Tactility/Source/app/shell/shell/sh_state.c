#include <Tactility/app/shell/shell/sh.h>
#include <Tactility/app/shell/shell/sh_parse.h>
#include <Tactility/app/shell/shell/sh_port.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// True if two absolute paths name the same directory. A textual match is enough
// in the common case; otherwise compare (st_dev, st_ino) so a symlinked spelling
// like /tmp vs /private/tmp still counts as equal.
static int same_dir(const char *a, const char *b)
{
    if (strcmp(a, b) == 0) return 1;
    struct stat sa, sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0) return 0;
    // Filesystems without real inodes (littlefs/FAT on device) report st_ino=0
    // for every entry, which would make any two directories compare equal.
    if (sa.st_ino == 0 || sb.st_ino == 0) return 0;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

void sh_state_init(sh_state *st)
{
    st->vars = NULL;
    st->last_status = 0;
    st->opt_errexit = 0;
    st->opt_nounset = 0;
    st->exiting = 0;
    st->exit_code = 0;
    st->brk = 0;
    st->cont = 0;
    st->loop_depth = 0;
    st->pos = NULL;
    st->npos = 0;
    st->arg0 = NULL;
    st->funcs = NULL;
    st->returning = 0;
    st->return_code = 0;
    st->call_depth = 0;
    st->scopes = NULL;
    st->parse_error = 0;

    // Seed directory-related vars so tilde/cd/pwd have sane defaults. Prefer the
    // real environment on host; fall back to the actual cwd for PWD.
    const char *home = getenv("HOME");
    if (home) sh_set(st, "HOME", home);
    // Establish the logical cwd. The physical cwd (sh_port_getcwd) is the source
    // of truth: on device it is the VFS's own tracked cwd, and $PWD is a stale
    // global left over from an earlier `sh` invocation (an interactive `cd`
    // updates the VFS cwd but not the environment). Only trust an inherited
    // absolute $PWD when it actually names that same directory -- that keeps a
    // symlinked spelling like /tmp (vs /private/tmp) on host, but discards a
    // stale /root when the real cwd is /root/share/regtest.
    char buf[512];
    sh_port_getcwd(buf, sizeof(buf));
    const char *pwd = getenv("PWD");
    if (!pwd || pwd[0] != '/' || !same_dir(pwd, buf)) pwd = buf;
    st->cwd = strdup(pwd);
    sh_set(st, "PWD", pwd);
    sh_export(st, "PWD");   // dash keeps $PWD exported into the environment

    const char *oldpwd = getenv("OLDPWD");
    if (oldpwd) sh_set(st, "OLDPWD", oldpwd);

    // $TMP is used pervasively by the spec suite for scratch files; honor the
    // inherited value (dash inherits it from the environment) so redirect
    // targets like `>$TMP/out` resolve.
    const char *tmp = getenv("TMP");
    if (tmp) sh_set(st, "TMP", tmp);

    // $SH points at the shell binary in the spec harness; cases invoke it as
    // `$SH -c ...` to test recursive/self invocation. Seed it from the
    // environment like dash does (a no-op on device, where getenv returns NULL).
    const char *sh = getenv("SH");
    if (sh) sh_set(st, "SH", sh);
}

static void free_pos(sh_state *st)
{
    for (int i = 0; i < st->npos; i++) free(st->pos[i]);
    free(st->pos);
    st->pos = NULL;
    st->npos = 0;
}

void sh_set_positional(sh_state *st, const char *arg0, char **params, int n)
{
    if (arg0) {
        free(st->arg0);
        st->arg0 = strdup(arg0);
    }
    free_pos(st);
    if (n > 0) {
        st->pos = malloc(n * sizeof(char *));
        for (int i = 0; i < n; i++) st->pos[i] = strdup(params[i]);
        st->npos = n;
    }
}

int sh_shift_positional(sh_state *st, int n)
{
    if (n < 0) n = 0;
    if (n > st->npos) return 1;
    for (int i = 0; i < n; i++) free(st->pos[i]);
    st->npos -= n;
    for (int i = 0; i < st->npos; i++) st->pos[i] = st->pos[i + n];
    return 0;
}

void sh_state_free(sh_state *st)
{
    sh_var *v = st->vars;
    while (v) {
        sh_var *next = v->next;
        free(v->name);
        free(v->value);
        free(v);
        v = next;
    }
    st->vars = NULL;
    free_pos(st);
    free(st->arg0);
    st->arg0 = NULL;
    free(st->cwd);
    st->cwd = NULL;

    sh_func *f = st->funcs;
    while (f) {
        sh_func *next = f->next;
        free(f->name);
        sh_free_node(f->body);
        free(f);
        f = next;
    }
    st->funcs = NULL;

    while (st->scopes) sh_scope_pop(st);
}

// ---- functions -------------------------------------------------------------

sh_func *sh_func_find(sh_state *st, const char *name)
{
    for (sh_func *f = st->funcs; f; f = f->next)
        if (strcmp(f->name, name) == 0) return f;
    return NULL;
}

void sh_func_define(sh_state *st, const char *name, struct node *body)
{
    sh_func *f = sh_func_find(st, name);
    if (f) {
        sh_free_node(f->body);
        f->body = body;
        return;
    }
    f = malloc(sizeof(*f));
    f->name = strdup(name);
    f->body = body;
    f->next = st->funcs;
    st->funcs = f;
}

int sh_func_unset(sh_state *st, const char *name)
{
    sh_func **pp = &st->funcs;
    while (*pp) {
        if (strcmp((*pp)->name, name) == 0) {
            sh_func *dead = *pp;
            *pp = dead->next;
            sh_free_node(dead->body);
            free(dead->name);
            free(dead);
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

// ---- local scopes ----------------------------------------------------------

void sh_scope_push(sh_state *st)
{
    sh_scope *s = malloc(sizeof(*s));
    s->saved = NULL;
    s->next = st->scopes;
    st->scopes = s;
}

void sh_scope_pop(sh_state *st)
{
    sh_scope *s = st->scopes;
    if (!s) return;
    st->scopes = s->next;
    // Restore in list order (most-recent local first) so duplicate `local`
    // declarations collapse back to the pre-call value.
    for (sh_saved_var *sv = s->saved; sv; ) {
        sh_saved_var *next = sv->next;
        if (sv->was_set) sh_set(st, sv->name, sv->value);
        else sh_unset(st, sv->name);
        free(sv->name);
        free(sv->value);
        free(sv);
        sv = next;
    }
    free(s);
}

int sh_local(sh_state *st, const char *name, const char *value)
{
    sh_scope *s = st->scopes;
    if (!s) return 1;   // not in a function
    const char *prior = sh_get(st, name);
    sh_saved_var *sv = malloc(sizeof(*sv));
    sv->name = strdup(name);
    sv->was_set = prior != NULL;
    sv->value = prior ? strdup(prior) : NULL;
    sv->next = s->saved;
    s->saved = sv;
    // `local NAME=value` assigns; bare `local NAME` leaves any current value
    // in place (dash keeps it, e.g. a repeated `local foo` after `local foo=bar`).
    if (value) sh_set(st, name, value);
    return 0;
}

static sh_var *find(sh_state *st, const char *name)
{
    for (sh_var *v = st->vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0) return v;
    }
    return NULL;
}

const char *sh_get(sh_state *st, const char *name)
{
    sh_var *v = find(st, name);
    return v ? v->value : NULL;
}

void sh_set(sh_state *st, const char *name, const char *value)
{
    sh_var *v = find(st, name);
    if (v) {
        free(v->value);
        v->value = strdup(value);
        if (v->exported) setenv(name, value, 1);
        return;
    }
    v = malloc(sizeof(*v));
    v->name = strdup(name);
    v->value = strdup(value);
    v->exported = 0;
    v->next = st->vars;
    st->vars = v;
}

void sh_export(sh_state *st, const char *name)
{
    sh_var *v = find(st, name);
    if (!v) {
        // `export NAME` before assignment: remember the intent so a later
        // `NAME=value` propagates to the environment (POSIX).
        sh_set(st, name, "");
        v = find(st, name);
        v->exported = 1;
        return;   // no value yet; don't seed the env with an empty string
    }
    v->exported = 1;
    setenv(name, v->value, 1);
}

void sh_unset(sh_state *st, const char *name)
{
    sh_var **pp = &st->vars;
    while (*pp) {
        if (strcmp((*pp)->name, name) == 0) {
            sh_var *dead = *pp;
            *pp = dead->next;
            if (dead->exported) unsetenv(name);
            free(dead->name);
            free(dead->value);
            free(dead);
            return;
        }
        pp = &(*pp)->next;
    }
}
