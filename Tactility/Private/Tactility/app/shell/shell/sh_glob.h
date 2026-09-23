// Shared shell pattern matcher for '*' and '?' (POSIX glob subset).
//
// Bracket classes [..] are intentionally out of scope for now. Reused by the
// ${v#..}/${v%..} strip operators and, later, by `case` and pathname globbing.
#pragma once

// Match string `s` against shell pattern `p` using '*' (any run, incl. empty)
// and '?' (exactly one char). Backslash escapes the next metacharacter.
// Returns 1 on match, 0 otherwise.
int sh_pattern_match(const char *p, const char *s);

// Strip a prefix/suffix of `s` matching pattern `p`. `longest` selects the
// greedy (##/%%) vs. minimal (#/%) variant. Returns a newly malloc'd string
// (caller frees); on no match, a copy of `s`.
char *sh_strip_prefix(const char *s, const char *p, int longest);
char *sh_strip_suffix(const char *s, const char *p, int longest);

// Expand `pat` against the filesystem (single-directory pathname globbing).
// On one or more matches, pushes them sorted into `out` and returns the count.
// Returns 0 (pushing nothing) when the pattern has no unescaped metachars, the
// directory part contains metachars, or nothing matched -- the caller keeps
// the word literal, dash-style.
#include "sh.h"
int sh_glob_pathnames(const char *pat, sh_fields *out);
