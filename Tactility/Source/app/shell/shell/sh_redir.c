// Ordered fd-level redirection over stdin/stdout/stderr, shared by the device
// and host builds. Redirection swaps the C streams (stdin/stdout/stderr are
// lvalue FILE* on both newlib and glibc), so builtins and the core see the
// redirected streams without any fd juggling.
#include <Tactility/app/shell/shell/sh_port.h>
#include <Tactility/app/shell/ShellBridge.h>
#include <errno.h>
#include <string.h>

static FILE **sh_stream_slot(int fd)
{
    switch (fd) {
        case 0: return &stdin;
        case 1: return &stdout;
        case 2: return &stderr;
        default: return NULL;
    }
}

int sh_redir_apply(const sh_redir_rt *items, int n, sh_redir_saved *saved)
{
    for (int i = 0; i < 3; i++) { saved->orig[i] = *sh_stream_slot(i); saved->touched[i] = 0; }
    saved->nopened = 0;

    for (int k = 0; k < n; k++) {
        const sh_redir_rt *it = &items[k];
        FILE **slot = sh_stream_slot(it->fd);
        if (!slot) { sh_redir_restore(saved); return -1; }  // fd > 2 unsupported
        /* Tactility: resolve against the shell's own working directory before opening. The C
         * library has a separate cwd that never changes, so a relative target like `> out.txt`
         * would otherwise be created at the filesystem root - where nothing is mounted, giving a
         * confusing ENOENT for a file the redirect was supposed to create. */
        char resolved[256];
        const char *target = it->path;
        if (it->path && shell_bridge_resolve(it->path, resolved, sizeof(resolved)) == 0) {
            target = resolved;
        }

        FILE *f = NULL;
        switch (it->op) {
            case SH_RD_IN:     f = fopen(target, "r"); break;
            case SH_RD_OUT:    f = fopen(target, "w"); break;
            case SH_RD_APPEND: f = fopen(target, "a"); break;
            case SH_RD_CLOSE:  f = fopen("/dev/null", it->fd == 0 ? "r" : "w"); break;
            case SH_RD_DUP: {
                FILE **src = sh_stream_slot(it->dupfd);
                if (!src) { sh_redir_restore(saved); return -1; }
                saved->touched[it->fd] = 1;
                *slot = *src;  // share the current target of dupfd
                continue;
            }
        }
        if (!f) {
            const char *p = (it->op == SH_RD_CLOSE) ? "/dev/null" : target;
            fprintf(stderr, "sh: cannot open %s: %s (errno %d)\n",
                    p, strerror(errno), errno);
            sh_redir_restore(saved);
            return -1;
        }
        if (saved->nopened < 8) saved->opened[saved->nopened++] = f;
        saved->touched[it->fd] = 1;
        *slot = f;
    }
    return 0;
}

void sh_redir_restore(sh_redir_saved *saved)
{
    for (int i = 0; i < 3; i++) {
        if (saved->touched[i]) { *sh_stream_slot(i) = saved->orig[i]; saved->touched[i] = 0; }
    }
    for (int i = 0; i < saved->nopened; i++) { fflush(saved->opened[i]); fclose(saved->opened[i]); }
    saved->nopened = 0;
}
