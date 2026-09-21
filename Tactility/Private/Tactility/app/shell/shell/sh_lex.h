#pragma once

typedef enum {
    T_WORD,      // a raw word, quotes preserved (resolved later by sh_expand)
    T_SEMI,      // ;
    T_AMPAMP,    // &&
    T_BARBAR,    // ||
    T_BAR,       // |
    T_LT,        // <
    T_GT,        // >
    T_GTGT,      // >>
    T_GTAMP,     // >&  (dup: fd>&n / fd>&-)
    T_LTAMP,     // <&  (dup: fd<&n / fd<&-)
    T_CLOBBER,   // >|  (force truncate; we have no noclobber, acts as >)
    T_DLESS,     // <<  (here-doc)
    T_DLESSDASH, // <<- (here-doc, strip leading tabs)
    T_LPAREN,    // (
    T_RPAREN,    // )
    T_DSEMI,     // ;;
    T_NEWLINE,   // significant line break / statement separator
    T_EOF
} tok_type;

typedef struct {
    tok_type type;
    char    *text;      // owned; T_WORD text, or (for T_DLESS*) the captured body
    int      hd_quoted; // T_DLESS*: delimiter was quoted -> body is not expanded
    int      rfd;       // redirection source fd (e.g. 2 in `2>`), -1 if N/A
} sh_tok;

typedef struct sh_toklist {
    sh_tok *toks;
    int     count;
    int     cap;
    int     error;   // set on unterminated quote etc.
} sh_toklist;

// Tokenize src into tl. Returns 0 on success, -1 on lex error.
int  sh_lex(const char *src, sh_toklist *tl);
void sh_toklist_free(sh_toklist *tl);
