#include <Tactility/app/shell/shell/sh_lex.h>
#include <stdlib.h>
#include <string.h>

static void push(sh_toklist *tl, tok_type type, char *text)
{
    if (tl->count == tl->cap) {
        tl->cap = tl->cap ? tl->cap * 2 : 16;
        tl->toks = realloc(tl->toks, tl->cap * sizeof(sh_tok));
    }
    tl->toks[tl->count].type = type;
    tl->toks[tl->count].text = text;
    tl->toks[tl->count].hd_quoted = 0;
    tl->toks[tl->count].rfd = -1;
    tl->count++;
}

// Push a redirection operator token carrying its source fd.
static void push_redir(sh_toklist *tl, tok_type type, int fd)
{
    push(tl, type, NULL);
    tl->toks[tl->count - 1].rfd = fd;
}

static int is_op_char(char c)
{
    return c == ';' || c == '&' || c == '|' || c == '<' || c == '>' ||
           c == '(' || c == ')';
}

// A growable char buffer for scan_word / scan_cmdsub.
typedef struct { char *buf; int len; int cap; } wbuf;
#define WPUT(w, ch) do { \
    if ((w)->len + 1 >= (w)->cap) { (w)->cap *= 2; (w)->buf = realloc((w)->buf, (w)->cap); } \
    (w)->buf[(w)->len++] = (ch); \
} while (0)

// Consume a command substitution $(...) beginning at src[j] (src[j]=='$',
// src[j+1]=='('), copying the whole balanced expression verbatim into `w`.
// Parens inside single/double quotes don't affect nesting; nested $() do.
// sh_expand later runs the inner command. Returns the index past the ')'.
static int scan_cmdsub(const char *src, int j, wbuf *w)
{
    WPUT(w, src[j]); j++;      // '$'
    WPUT(w, src[j]); j++;      // '('
    int depth = 1;
    while (src[j] && depth > 0) {
        char d = src[j];
        if (d == '\'') {
            WPUT(w, d); j++;
            while (src[j] && src[j] != '\'') { WPUT(w, src[j]); j++; }
            if (src[j] == '\'') { WPUT(w, src[j]); j++; }
        } else if (d == '"') {
            WPUT(w, d); j++;
            while (src[j] && src[j] != '"') {
                if (src[j] == '$' && src[j + 1] == '(') { j = scan_cmdsub(src, j, w); }
                else if (src[j] == '\\' && src[j + 1]) { WPUT(w, src[j]); j++; WPUT(w, src[j]); j++; }
                else { WPUT(w, src[j]); j++; }
            }
            if (src[j] == '"') { WPUT(w, src[j]); j++; }
        } else if (d == '\\' && src[j + 1]) {
            WPUT(w, d); j++; WPUT(w, src[j]); j++;
        } else {
            if (d == '(') depth++;
            else if (d == ')') depth--;
            WPUT(w, d); j++;
        }
    }
    return j;
}

// Consume a backtick command substitution `...` beginning at src[j]
// (src[j]=='`'), emitting it into `w` as an equivalent $(...) so the rest of
// the pipeline (which only understands $()) handles it uniformly. Inside
// backticks the only active escapes are \` \\ \$; other backslashes are kept.
// Returns the index past the closing backtick.
static int scan_backtick(const char *src, int j, wbuf *w)
{
    WPUT(w, '$'); WPUT(w, '(');
    j++;   // opening backtick
    // Backtick content is always a command list, never arithmetic. If it opens
    // with '(', emit a separating space so the rewrite becomes `$( (...` rather
    // than `$((...`, which the expander would misread as arithmetic (bash bug
    // #2337: `((cmd) || (cmd))` in backticks is nested subshells, not `$(( ))`).
    if (src[j] == '(') WPUT(w, ' ');
    while (src[j] && src[j] != '`') {
        if (src[j] == '\\' && (src[j + 1] == '`' || src[j + 1] == '\\' ||
                               src[j + 1] == '$')) {
            j++; WPUT(w, src[j]); j++;
        } else {
            WPUT(w, src[j]); j++;
        }
    }
    if (src[j] == '`') j++;   // closing backtick
    WPUT(w, ')');
    return j;
}

// Consume a ${...} parameter expansion beginning at src[j] (src[j]=='$',
// src[j+1]=='{'), copying the whole balanced expression verbatim into `w`
// (including any spaces in default/strip words). Respects nested ${}, $(),
// and quotes. Returns the index past the closing '}'.
static int scan_braceparam(const char *src, int j, wbuf *w)
{
    WPUT(w, src[j]); j++;      // '$'
    WPUT(w, src[j]); j++;      // '{'
    int depth = 1;
    while (src[j] && depth > 0) {
        char d = src[j];
        if (d == '\\' && src[j + 1]) { WPUT(w, d); j++; WPUT(w, src[j]); j++; continue; }
        if (d == '\'') {
            WPUT(w, d); j++;
            while (src[j] && src[j] != '\'') { WPUT(w, src[j]); j++; }
            if (src[j] == '\'') { WPUT(w, src[j]); j++; }
            continue;
        }
        if (d == '"') {
            WPUT(w, d); j++;
            while (src[j] && src[j] != '"') {
                if (src[j] == '\\' && src[j + 1]) { WPUT(w, src[j]); j++; WPUT(w, src[j]); j++; }
                else { WPUT(w, src[j]); j++; }
            }
            if (src[j] == '"') { WPUT(w, src[j]); j++; }
            continue;
        }
        if (d == '$' && src[j + 1] == '(') { j = scan_cmdsub(src, j, w); continue; }
        if (d == '$' && src[j + 1] == '{') { depth++; WPUT(w, d); j++; WPUT(w, src[j]); j++; continue; }
        if (d == '{') depth++;
        else if (d == '}') { depth--; WPUT(w, d); j++; continue; }
        WPUT(w, d); j++;
    }
    return j;
}

// Scan a word starting at src[*i], preserving quotes. Advances *i.
// Returns an owned string, or NULL on unterminated quote.
static char *scan_word(const char *src, int *i)
{
    wbuf w = { malloc(32), 0, 32 };
    int j = *i;

#define PUT(ch) WPUT(&w, ch)

    while (src[j]) {
        char c = src[j];
        /* Tactility: '\r' ends a word as well, so that a script saved with CRLF line endings
         * lexes the same as one with LF. Without this, "fi\r\n" scans as the word "fi\r", which
         * does not match the "fi" keyword - reported as a bogus "expected 'fi'" on a valid
         * script. The main lexer loop already skips '\r' as whitespace; this is the same rule
         * applied inside word scanning. */
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || is_op_char(c)) {
            break;
        }
        if (c == '\'') {
            PUT(c); j++;
            while (src[j] && src[j] != '\'') { PUT(src[j]); j++; }
            if (src[j] != '\'') { free(w.buf); return NULL; }
            PUT(src[j]); j++;
        } else if (c == '"') {
            PUT(c); j++;
            while (src[j] && src[j] != '"') {
                if (src[j] == '$' && src[j + 1] == '(') { j = scan_cmdsub(src, j, &w); }
                else if (src[j] == '`') { j = scan_backtick(src, j, &w); }
                else if (src[j] == '\\' && src[j + 1] == '\n') { j += 2; }  // line continuation
                else if (src[j] == '\\' && src[j + 1]) { PUT(src[j]); j++; PUT(src[j]); j++; }
                else { PUT(src[j]); j++; }
            }
            if (src[j] != '"') { free(w.buf); return NULL; }
            PUT(src[j]); j++;
        } else if (c == '`') {
            j = scan_backtick(src, j, &w);
        } else if (c == '$' && src[j + 1] == '(') {
            // Command substitution $(...): keep the whole balanced expression
            // (spaces, operators, nested $()) in the word; sh_expand runs it.
            j = scan_cmdsub(src, j, &w);
        } else if (c == '$' && src[j + 1] == '{') {
            // Parameter expansion ${...}: keep it whole (default/strip words may
            // contain spaces); sh_expand parses the operators.
            j = scan_braceparam(src, j, &w);
        } else if (c == '\\') {
            if (src[j + 1] == '\n') { j += 2; continue; }  // line continuation
            PUT(c); j++;
            if (src[j]) { PUT(src[j]); j++; }
        } else {
            PUT(c); j++;
        }
    }
    w.buf[w.len] = '\0';
    *i = j;
    return w.buf;
#undef PUT
}

// Resolve a here-doc delimiter word (quotes preserved, as scanned) into the
// bare delimiter string. Sets *quoted if any quoting was present (which means
// the body must not be expanded). Returns an owned string.
static char *heredoc_delim(const char *word, int *quoted)
{
    wbuf w = { malloc(16), 0, 16 };
    *quoted = 0;
    for (int j = 0; word[j]; ) {
        char c = word[j];
        if (c == '\'') {
            *quoted = 1; j++;
            while (word[j] && word[j] != '\'') { WPUT(&w, word[j]); j++; }
            if (word[j] == '\'') j++;
        } else if (c == '"') {
            *quoted = 1; j++;
            while (word[j] && word[j] != '"') { WPUT(&w, word[j]); j++; }
            if (word[j] == '"') j++;
        } else if (c == '\\' && word[j + 1]) {
            *quoted = 1; WPUT(&w, word[j + 1]); j += 2;
        } else {
            WPUT(&w, c); j++;
        }
    }
    w.buf[w.len] = '\0';
    return w.buf;
}

// Collect a here-doc body from src starting at *i (just past the introducing
// newline). Consumes lines until one equals `delim` (leading tabs stripped from
// both body lines and the terminator when `dash`). Stores the body in the
// token at index `tok` and advances *i past the terminator line.
static void collect_heredoc(const char *src, int *i, const char *delim,
                            int dash, sh_toklist *tl, int tok)
{
    wbuf w = { malloc(32), 0, 32 };
    size_t dlen = strlen(delim);
    int j = *i;
    while (src[j]) {
        int cs = j;
        if (dash) while (src[cs] == '\t') cs++;
        int nl = cs;
        while (src[nl] && src[nl] != '\n') nl++;
        // Terminator line?
        if ((size_t)(nl - cs) == dlen && strncmp(src + cs, delim, dlen) == 0) {
            j = src[nl] == '\n' ? nl + 1 : nl;
            break;
        }
        for (int k = cs; k < nl; k++) WPUT(&w, src[k]);
        WPUT(&w, '\n');
        if (src[nl] != '\n') { j = nl; break; }   // EOF without terminator
        j = nl + 1;
    }
    w.buf[w.len] = '\0';
    tl->toks[tok].text = w.buf;
    *i = j;
}

// Lex a redirection operator at src[*i] (which is '<' or '>'), with `fd` the
// explicit source fd or -1 for the operator default. Handles here-doc
// registration via the pend arrays. Advances *i past the operator.
static void lex_redir(const char *src, int *i, int fd, sh_toklist *tl,
                      int *pend_tok, int *pend_dash, int *npend)
{
    int j = *i;
    if (src[j] == '<') {
        if (src[j + 1] == '<') {
            int dash = src[j + 2] == '-';
            push(tl, dash ? T_DLESSDASH : T_DLESS, NULL);
            if (*npend < 8) { pend_tok[*npend] = tl->count - 1; pend_dash[*npend] = dash; (*npend)++; }
            j += dash ? 3 : 2;
        } else if (src[j + 1] == '&') {
            push_redir(tl, T_LTAMP, fd < 0 ? 0 : fd); j += 2;
        } else {
            push_redir(tl, T_LT, fd < 0 ? 0 : fd); j += 1;
        }
    } else { // '>'
        if (src[j + 1] == '>')      { push_redir(tl, T_GTGT, fd < 0 ? 1 : fd); j += 2; }
        else if (src[j + 1] == '&') { push_redir(tl, T_GTAMP, fd < 0 ? 1 : fd); j += 2; }
        else if (src[j + 1] == '|') { push_redir(tl, T_CLOBBER, fd < 0 ? 1 : fd); j += 2; }
        else                        { push_redir(tl, T_GT, fd < 0 ? 1 : fd); j += 1; }
    }
    *i = j;
}

int sh_lex(const char *src, sh_toklist *tl)
{
    tl->toks = NULL;
    tl->count = 0;
    tl->cap = 0;
    tl->error = 0;

    // Here-docs whose bodies are captured when the current line's newline is
    // reached. Each entry is the index of the T_DLESS* token; its delimiter is
    // the following T_WORD token, and its body is stored back on it.
    int pend_tok[8], pend_dash[8], npend = 0;

    int i = 0;
    while (src[i]) {
        char c = src[i];
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
        if (c == '\\' && src[i + 1] == '\n') { i += 2; continue; }  // line continuation
        if (c == '#') { while (src[i] && src[i] != '\n') i++; continue; }
        if (c == '\n') {
            push(tl, T_NEWLINE, NULL);
            i++;
            for (int k = 0; k < npend; k++) {
                int dt = pend_tok[k];
                if (dt + 1 >= tl->count || tl->toks[dt + 1].type != T_WORD ||
                    tl->toks[dt + 1].text == NULL) {
                    continue; // no delimiter word; sh_parse reports the error
                }
                int quoted = 0;
                char *delim = heredoc_delim(tl->toks[dt + 1].text, &quoted);
                collect_heredoc(src, &i, delim, pend_dash[k], tl, dt);
                tl->toks[dt].hd_quoted = quoted;
                free(delim);
            }
            npend = 0;
            continue;
        }
        if (c == ';') {
            if (src[i + 1] == ';') { push(tl, T_DSEMI, NULL); i += 2; }
            else { push(tl, T_SEMI, NULL); i++; }
            continue;
        }
        if (c == '(') { push(tl, T_LPAREN, NULL); i++; continue; }
        if (c == ')') { push(tl, T_RPAREN, NULL); i++; continue; }
        if (c == '&') {
            if (src[i + 1] == '&') { push(tl, T_AMPAMP, NULL); i += 2; }
            else { i++; }  // lone & (background) unsupported: ignore
            continue;
        }
        if (c == '|') {
            if (src[i + 1] == '|') { push(tl, T_BARBAR, NULL); i += 2; }
            else { push(tl, T_BAR, NULL); i++; }
            continue;
        }
        if (c == '<' || c == '>') {
            lex_redir(src, &i, -1, tl, pend_tok, pend_dash, &npend);
            continue;
        }
        // fd-prefixed redirection: digits immediately abutting '<' or '>'
        // (e.g. `2>`, `2>>`, `2>&1`). A digit run not followed by a redirect
        // operator is an ordinary word.
        if (c >= '0' && c <= '9') {
            int k = i;
            while (src[k] >= '0' && src[k] <= '9') k++;
            if (src[k] == '<' || src[k] == '>') {
                int fd = (int)strtol(src + i, NULL, 10);
                i = k;
                lex_redir(src, &i, fd, tl, pend_tok, pend_dash, &npend);
                continue;
            }
        }
        // word
        char *w = scan_word(src, &i);
        if (!w) { tl->error = 1; sh_toklist_free(tl); return -1; }
        push(tl, T_WORD, w);
    }
    push(tl, T_EOF, NULL);
    return 0;
}

void sh_toklist_free(sh_toklist *tl)
{
    for (int i = 0; i < tl->count; i++) {
        free(tl->toks[i].text);
    }
    free(tl->toks);
    tl->toks = NULL;
    tl->count = tl->cap = 0;
}
