// Platform interface for the shell core. The device and host builds provide
// their own implementation (sh_port_esp.c / sh_port_host.c). This is the only
// seam between the pure-C core and the outside world.
#pragma once

#include <stdio.h>

// Run an external (non-builtin) command with a fully-expanded argv.
// Returns the command's exit status. Sets *found to 1 if the command existed,
// 0 if it could not be located (so the core can report "not found").
int sh_port_run_external(int argc, char **argv, int *found);

// Ordered fd-level redirection over fds 0/1/2 (stdin/stdout/stderr). Each item
// points a source fd at a file, at another fd's current target (dup), or at a
// discard sink (close). Applied left to right so `>f 2>&1` and `2>&1 >f`
// differ. Only fds 0/1/2 are supported — a source fd outside that range fails.
typedef enum {
    SH_RD_IN,      // fd < path
    SH_RD_OUT,     // fd > path  (truncate)
    SH_RD_APPEND,  // fd >> path
    SH_RD_DUP,     // fd >& dupfd
    SH_RD_CLOSE,   // fd >&-
} sh_rd_op;

typedef struct {
    int         fd;    // source fd (0/1/2)
    sh_rd_op    op;
    const char *path;  // IN/OUT/APPEND target
    int         dupfd; // DUP target fd
} sh_redir_rt;

typedef struct {
    FILE *orig[3];      // original stdin/stdout/stderr
    int   touched[3];   // which streams were reassigned
    FILE *opened[8];    // files we opened and must close on restore
    int   nopened;
} sh_redir_saved;

// Apply the ordered list; save originals into *saved. Returns 0 on success, -1
// on failure (partial work is unwound; restore must NOT be called after -1).
int  sh_redir_apply(const sh_redir_rt *items, int n, sh_redir_saved *saved);
void sh_redir_restore(sh_redir_saved *saved);

// Fill buf with a scratch temp-file path used for temp-file pipe semantics.
// `which` (0/1) selects between two distinct names so a pipeline stage can read
// one temp while writing another.
void sh_port_tmpfile(int which, char *buf, int bufsz);

// Working directory (cd / pwd builtins).
int  sh_port_chdir(const char *path);       // 0 on success
void sh_port_getcwd(char *buf, int bufsz);
