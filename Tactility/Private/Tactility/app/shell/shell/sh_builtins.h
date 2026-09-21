#pragma once
#include "sh.h"

// If argv[0] names a shell builtin, run it, store its status in *status, and
// return 1. Otherwise return 0 (caller should try an external command).
int sh_run_builtin(sh_state *st, int argc, char **argv, int *status);
