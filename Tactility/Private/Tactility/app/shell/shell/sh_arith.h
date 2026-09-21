// POSIX arithmetic expansion `$(( ))` evaluator.
#pragma once

#include "sh.h"

// Evaluate arithmetic expression `expr` (signed long, C/POSIX operator set).
// Returns 0 on success and writes *out; nonzero on error (division by zero,
// bad token, etc.). `st` is used to read/write shell variables. On error, an
// English message is left in *errmsg (points at a static buffer; may be NULL
// on success). Pass errmsg=NULL to ignore.
int sh_arith_eval(sh_state *st, const char *expr, long *out, const char **errmsg);
