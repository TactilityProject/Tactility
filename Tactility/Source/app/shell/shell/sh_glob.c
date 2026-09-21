#include <Tactility/app/shell/shell/sh_glob.h>
#include <Tactility/app/shell/ShellBridge.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// POSIX character class test, e.g. [[:alpha:]]. `name`/`len` name the class.
static int class_match(const char *name, int len, unsigned char c)
{
    if (len == 5 && !memcmp(name, "alpha", 5)) return isalpha(c);
    if (len == 5 && !memcmp(name, "digit", 5)) return isdigit(c);
    if (len == 5 && !memcmp(name, "alnum", 5)) return isalnum(c);
    if (len == 5 && !memcmp(name, "space", 5)) return isspace(c);
    if (len == 5 && !memcmp(name, "upper", 5)) return isupper(c);
    if (len == 5 && !memcmp(name, "lower", 5)) return islower(c);
    if (len == 5 && !memcmp(name, "print", 5)) return isprint(c);
    if (len == 5 && !memcmp(name, "graph", 5)) return isgraph(c);
    if (len == 5 && !memcmp(name, "punct", 5)) return ispunct(c);
    if (len == 5 && !memcmp(name, "cntrl", 5)) return iscntrl(c);
    if (len == 5 && !memcmp(name, "blank", 5)) return (c == ' ' || c == '\t');
    if (len == 6 && !memcmp(name, "xdigit", 6)) return isxdigit(c);
    return 0;
}

// Try to match a bracket expression `[...]` at pattern position *pp (which
// points just past the '['). On success, advance *pp past the closing ']' and
// return 1/0 for whether byte c matched. If the bracket is unterminated (no
// closing ']'), return -1 and leave *pp unchanged so '[' is treated literally.
static int bracket_match(const char **pp, unsigned char c)
{
    const char *p = *pp;
    int negate = 0;
    if (*p == '!' || *p == '^') { negate = 1; p++; }
    // A ']' immediately after '[' or '[!' is a literal member, not the closer.
    const char *start = p;
    int matched = 0;
    for (;;) {
        if (!*p) return -1;                        // unterminated -> literal '['
        if (*p == ']' && p != start) { p++; break; }
        if (p[0] == '[' && p[1] == ':') {          // POSIX class [[:name:]]
            const char *cn = p + 2;
            const char *e = cn;
            while (*e && !(e[0] == ':' && e[1] == ']')) e++;
            if (e[0] == ':' && e[1] == ']') {
                if (class_match(cn, (int)(e - cn), c)) matched = 1;
                p = e + 2;
                continue;
            }
        }
        unsigned char lo = (unsigned char)*p;
        if (p[1] == '-' && p[2] && p[2] != ']') {  // range a-z
            unsigned char hi = (unsigned char)p[2];
            if (c >= lo && c <= hi) matched = 1;
            p += 3;
        } else {
            if (c == lo) matched = 1;
            p++;
        }
    }
    *pp = p;
    return negate ? !matched : matched;
}

int sh_pattern_match(const char *p, const char *s)
{
    while (*p) {
        if (*p == '[') {
            const char *pp = p + 1;
            int r = bracket_match(&pp, (unsigned char)*s);
            if (r >= 0) {
                if (!*s || !r) return 0;
                p = pp; s++;
                continue;
            }
            // fall through: unterminated bracket, '[' is literal
        }
        if (*p == '*') {
            p++;
            if (!*p) return 1;                 // trailing '*' matches the rest
            for (const char *t = s; ; t++) {
                if (sh_pattern_match(p, t)) return 1;
                if (!*t) return 0;
            }
        } else if (*p == '?') {
            if (!*s) return 0;
            p++; s++;
        } else if (*p == '\\' && p[1]) {
            if (*s != p[1]) return 0;
            p += 2; s++;
        } else {
            if (*s != *p) return 0;
            p++; s++;
        }
    }
    return *s == 0;
}

char *sh_strip_prefix(const char *s, const char *p, int longest)
{
    int n = (int)strlen(s);
    int best = -1;
    char *tmp = malloc(n + 1);
    for (int i = 0; i <= n; i++) {
        memcpy(tmp, s, i);
        tmp[i] = 0;
        if (sh_pattern_match(p, tmp)) {
            best = i;
            if (!longest) break;
        }
    }
    free(tmp);
    if (best < 0) return strdup(s);
    return strdup(s + best);
}

char *sh_strip_suffix(const char *s, const char *p, int longest)
{
    int n = (int)strlen(s);
    int best = -1;                             // index where kept prefix ends
    // Shortest suffix first (i = n, empty) up to longest (i = 0).
    for (int i = n; i >= 0; i--) {
        if (sh_pattern_match(p, s + i)) {
            best = i;
            if (!longest) break;
        }
    }
    if (best < 0) return strdup(s);
    char *r = malloc(best + 1);
    memcpy(r, s, best);
    r[best] = 0;
    return r;
}

// ---- pathname expansion -----------------------------------------------------
// Single-directory globbing: the pattern may carry a literal
// directory prefix (no metachars before the last '/'); only the final component
// is matched, via opendir/readdir (works on host libc and the ESP-IDF VFS).
// No recursion, no metachars across '/' components. Results are sorted.

#include <Tactility/app/shell/shell/sh.h>
#include <dirent.h>

static int name_cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int sh_glob_pathnames(const char *pat, sh_fields *out)
{
    // Locate the last '/' and reject metachars in the directory part.
    int lastslash = -1, meta = 0;
    for (int i = 0; pat[i]; i++) {
        char c = pat[i];
        if (c == '\\' && pat[i + 1]) { i++; continue; }
        if (c == '/') {
            if (meta) return 0;    // glob in a dir component: unsupported
            lastslash = i;
        } else if (c == '*' || c == '?' || c == '[') {
            meta = 1;
        }
    }
    if (!meta) return 0;

    char dir[512];
    const char *base = pat + lastslash + 1;
    if (lastslash < 0) strcpy(dir, ".");
    else if (lastslash == 0) strcpy(dir, "/");
    else {
        if (lastslash >= (int)sizeof(dir)) return 0;
        memcpy(dir, pat, lastslash);
        dir[lastslash] = 0;
    }

    /* Tactility: resolve the directory against the shell's working directory. The C library keeps
     * its own cwd which never changes, so "." here would be the filesystem root rather than where
     * the user actually is - and `*.txt` would silently match nothing. */
    char resolved_dir[512];
    if (shell_bridge_resolve(dir, resolved_dir, sizeof(resolved_dir)) == 0) {
        memcpy(dir, resolved_dir, sizeof(dir) < sizeof(resolved_dir) ? sizeof(dir) : sizeof(resolved_dir));
        dir[sizeof(dir) - 1] = 0;
    }

    DIR *d = opendir(dir);
    if (!d) return 0;

    sh_fields m;
    sh_fields_init(&m);
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *nm = e->d_name;
        // Dotfiles only match a pattern that starts with a literal '.';
        // never generate '.' or '..'.
        if (nm[0] == '.') {
            if (base[0] != '.') continue;
            if (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0)) continue;
        }
        if (sh_pattern_match(base, nm)) sh_fields_push(&m, nm);
    }
    closedir(d);
    if (m.count == 0) { sh_fields_free(&m); return 0; }

    qsort(m.items, m.count, sizeof(char *), name_cmp);
    for (int i = 0; i < m.count; i++) {
        if (lastslash < 0) {
            sh_fields_push(out, m.items[i]);
        } else {
            char full[1024];
            snprintf(full, sizeof(full), "%.*s/%s", lastslash ? lastslash : 0,
                     lastslash ? pat : "", m.items[i]);
            if (lastslash == 0) snprintf(full, sizeof(full), "/%s", m.items[i]);
            sh_fields_push(out, full);
        }
    }
    int n = m.count;
    sh_fields_free(&m);
    return n;
}
