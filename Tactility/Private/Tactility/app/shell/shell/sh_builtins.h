#pragma once
#include "sh.h"

#ifdef __cplusplus
extern "C" {
#endif

// If argv[0] names a shell builtin, run it, store its status in *status, and
// return 1. Otherwise return 0 (caller should try an external command).
// `eval` and `source`/`.` run nested programs, so the executor handles those itself.
int sh_run_builtin(sh_state *st, int argc, char **argv, int *status);

#ifdef __cplusplus
}
#endif
