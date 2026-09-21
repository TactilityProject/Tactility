#include <Tactility/app/shell/shell/sh_parse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    sh_toklist *tl;
    int pos;
    const char *err;
} P;

static sh_tok *cur(P *p)  { return &p->tl->toks[p->pos]; }
static tok_type curt(P *p){ return p->tl->toks[p->pos].type; }
static tok_type peekt(P *p){ return p->tl->toks[p->pos].type == T_EOF ? T_EOF : p->tl->toks[p->pos + 1].type; }
static void adv(P *p)     { if (p->tl->toks[p->pos].type != T_EOF) p->pos++; }

// Skip newlines/semicolons acting as blank separators.
static void skip_seps(P *p)
{
    while (curt(p) == T_NEWLINE || curt(p) == T_SEMI) adv(p);
}
static void skip_newlines(P *p)
{
    while (curt(p) == T_NEWLINE) adv(p);
}

static node *new_node(node_kind k)
{
    node *n = calloc(1, sizeof(node));
    n->kind = k;
    return n;
}

// Is the current token a bare word equal to `kw`?
static int is_word(P *p, const char *kw)
{
    sh_tok *t = cur(p);
    return t->type == T_WORD && t->text && strcmp(t->text, kw) == 0;
}

static int is_reserved(P *p)
{
    static const char *kw[] = { "then","else","elif","fi","do","done","esac","}", NULL };
    if (curt(p) != T_WORD) return 0;
    for (int i = 0; kw[i]; i++) if (is_word(p, kw[i])) return 1;
    return 0;
}

static node *parse_list(P *p);
static node *parse_and_or(P *p);
static node *parse_pipeline(P *p);
static node *parse_command(P *p);

static void push_child(node *n, node *c)
{
    n->children = realloc(n->children, (n->nchild + 1) * sizeof(node *));
    n->children[n->nchild++] = c;
}

// --- redirections -----------------------------------------------------------

static int is_redir_tok(tok_type t)
{
    return t == T_LT || t == T_GT || t == T_GTGT || t == T_GTAMP ||
           t == T_LTAMP || t == T_CLOBBER || t == T_DLESS || t == T_DLESSDASH;
}

static sh_redir *push_redir(node *n)
{
    n->redirs = realloc(n->redirs, (n->nredir + 1) * sizeof(sh_redir));
    sh_redir *r = &n->redirs[n->nredir++];
    r->kind = R_OUT; r->fd = 1; r->dupfd = 0; r->word = NULL; r->heredoc_quoted = 0;
    return r;
}

static int all_digits(const char *s)
{
    if (!*s) return 0;
    for (; *s; s++) if (*s < '0' || *s > '9') return 0;
    return 1;
}

// Parse the redirection at the current token onto `n`. Returns 1 on success,
// 0 on error (p->err set).
static int parse_redir(P *p, node *n)
{
    tok_type t = curt(p);
    int fd = cur(p)->rfd;
    // No explicit fd digit (rfd == -1): default to stdin for input operators,
    // stdout for output operators.
    if (fd < 0) {
        fd = (t == T_LT || t == T_LTAMP || t == T_DLESS || t == T_DLESSDASH)
             ? 0 : 1;
    }

    if (t == T_DLESS || t == T_DLESSDASH) {
        char *body = cur(p)->text ? strdup(cur(p)->text) : strdup("");
        int quoted = cur(p)->hd_quoted;
        adv(p);
        if (curt(p) != T_WORD) { free(body); p->err = "expected here-doc delimiter"; return 0; }
        sh_redir *r = push_redir(n);
        r->kind = R_HEREDOC; r->fd = 0; r->word = body; r->heredoc_quoted = quoted;
        adv(p);
        return 1;
    }

    if (t == T_GTAMP || t == T_LTAMP) {
        adv(p);
        if (curt(p) != T_WORD) { p->err = "expected fd or filename after >&"; return 0; }
        const char *tgt = cur(p)->text;
        sh_redir *r = push_redir(n);
        r->fd = fd;
        if (all_digits(tgt)) { r->kind = R_DUP; r->dupfd = (int)strtol(tgt, NULL, 10); }
        else if (strcmp(tgt, "-") == 0) { r->kind = R_CLOSE; }
        else { r->kind = (t == T_GTAMP) ? R_OUT : R_IN; r->word = strdup(tgt); }
        adv(p);
        return 1;
    }

    // T_LT / T_GT / T_GTGT / T_CLOBBER: filename target.
    redir_kind rk = (t == T_LT) ? R_IN : (t == T_GTGT) ? R_APPEND : R_OUT;
    adv(p);
    if (curt(p) != T_WORD) { p->err = "expected filename after redirection"; return 0; }
    sh_redir *r = push_redir(n);
    r->kind = rk; r->fd = fd; r->word = strdup(cur(p)->text);
    adv(p);
    return 1;
}

// Consume any trailing redirections onto a compound command. Returns 1/0.
static int parse_redir_list(P *p, node *n)
{
    while (is_redir_tok(curt(p))) {
        if (!parse_redir(p, n)) return 0;
    }
    return 1;
}

// --- simple command ---------------------------------------------------------

static int looks_like_assign(const char *w)
{
    if (!(*w == '_' || (*w >= 'A' && *w <= 'Z') || (*w >= 'a' && *w <= 'z'))) return 0;
    const char *q = w + 1;
    while (*q && *q != '=') {
        if (!(*q == '_' || (*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') ||
              (*q >= '0' && *q <= '9'))) return 0;
        q++;
    }
    return *q == '=';
}

static node *parse_simple(P *p)
{
    node *n = new_node(N_SIMPLE);
    int seen_word = 0;

    for (;;) {
        tok_type t = curt(p);
        if (t == T_WORD) {
            if (!seen_word && looks_like_assign(cur(p)->text)) {
                n->assigns = realloc(n->assigns, (n->nassign + 1) * sizeof(char *));
                n->assigns[n->nassign++] = strdup(cur(p)->text);
                adv(p);
                continue;
            }
            // Reserved words are only special in command position (first word);
            // as arguments they are ordinary words (e.g. `echo done`).
            if (!seen_word && is_reserved(p)) break;
            n->words = realloc(n->words, (n->nword + 1) * sizeof(char *));
            n->words[n->nword++] = strdup(cur(p)->text);
            seen_word = 1;
            adv(p);
        } else if (is_redir_tok(t)) {
            // Redirects may appear before, between, or after words and do not
            // count as the command word (so `FOO=x >f BAR=y cmd` still assigns).
            if (!parse_redir(p, n)) { sh_free_node(n); return NULL; }
        } else {
            break;
        }
    }
    if (n->nword == 0 && n->nassign == 0 && n->nredir == 0) {
        p->err = "expected command";
        sh_free_node(n);
        return NULL;
    }
    return n;
}

// --- control flow -----------------------------------------------------------

// Collect the raw words after `for NAME in` up to a separator.
static node *parse_for(P *p)
{
    adv(p); // 'for'
    if (curt(p) != T_WORD) { p->err = "expected name after for"; return NULL; }
    // The loop variable must be a valid name (dash rejects e.g. `for -`).
    const char *nm = cur(p)->text;
    if (!(nm[0] == '_' || (nm[0] >= 'a' && nm[0] <= 'z') || (nm[0] >= 'A' && nm[0] <= 'Z'))) {
        p->err = "bad for loop variable"; return NULL;
    }
    for (const char *q = nm + 1; *q; q++)
        if (!(*q == '_' || (*q >= '0' && *q <= '9') ||
              (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z'))) {
            p->err = "bad for loop variable"; return NULL;
        }
    node *n = new_node(N_FOR);
    n->for_name = strdup(nm);
    adv(p);
    skip_newlines(p);   // `for i <newline> in ...` is legal (`for i; in` is not)
    if (is_word(p, "in")) {
        adv(p);
        while (curt(p) == T_WORD && !is_reserved(p)) {
            n->for_words = realloc(n->for_words, (n->for_nword + 1) * sizeof(char *));
            n->for_words[n->for_nword++] = strdup(cur(p)->text);
            adv(p);
        }
    } else {
        // `for x; do ...` with no `in`: iterate over the positional params.
        n->for_implicit = 1;
    }
    skip_seps(p);
    if (!is_word(p, "do")) { p->err = "expected 'do'"; sh_free_node(n); return NULL; }
    adv(p);
    n->body = parse_list(p);
    if (!n->body) { sh_free_node(n); return NULL; }
    if (n->body->nchild == 0) { p->err = "empty do/done body"; sh_free_node(n); return NULL; }
    if (!is_word(p, "done")) { p->err = "expected 'done'"; sh_free_node(n); return NULL; }
    adv(p);
    return n;
}

static node *parse_while(P *p)
{
    int until = is_word(p, "until");
    adv(p); // 'while' / 'until'
    node *n = new_node(N_WHILE);
    n->until = until;
    n->cond = parse_list(p);
    if (!n->cond) { sh_free_node(n); return NULL; }
    if (!is_word(p, "do")) { p->err = "expected 'do'"; sh_free_node(n); return NULL; }
    adv(p);
    n->body = parse_list(p);
    if (!n->body) { sh_free_node(n); return NULL; }
    if (n->body->nchild == 0) { p->err = "empty do/done body"; sh_free_node(n); return NULL; }
    if (!is_word(p, "done")) { p->err = "expected 'done'"; sh_free_node(n); return NULL; }
    adv(p);
    return n;
}

static void push_clause(node *n, node *cond, node *body)
{
    n->conds = realloc(n->conds, (n->nclause + 1) * sizeof(node *));
    n->bodies = realloc(n->bodies, (n->nclause + 1) * sizeof(node *));
    n->conds[n->nclause] = cond;
    n->bodies[n->nclause] = body;
    n->nclause++;
}

static node *parse_if(P *p)
{
    adv(p); // 'if'
    node *n = new_node(N_IF);
    for (;;) {
        node *cond = parse_list(p);
        if (!cond) { sh_free_node(n); return NULL; }
        if (!is_word(p, "then")) { p->err = "expected 'then'"; sh_free_node(cond); sh_free_node(n); return NULL; }
        adv(p);
        node *body = parse_list(p);
        if (!body) { sh_free_node(cond); sh_free_node(n); return NULL; }
        if (body->nchild == 0) { p->err = "empty then body"; sh_free_node(cond); sh_free_node(body); sh_free_node(n); return NULL; }
        push_clause(n, cond, body);
        if (is_word(p, "elif")) { adv(p); continue; }
        break;
    }
    if (is_word(p, "else")) {
        adv(p);
        n->else_body = parse_list(p);
        if (!n->else_body) { sh_free_node(n); return NULL; }
        if (n->else_body->nchild == 0) { p->err = "empty else body"; sh_free_node(n); return NULL; }
    }
    if (!is_word(p, "fi")) { p->err = "expected 'fi'"; sh_free_node(n); return NULL; }
    adv(p);
    return n;
}

// case WORD in [(] pat [| pat]... ) list ;; ... esac
static node *parse_case(P *p)
{
    adv(p); // 'case'
    if (curt(p) != T_WORD) { p->err = "expected word after case"; return NULL; }
    node *n = new_node(N_CASE);
    n->case_word = strdup(cur(p)->text);
    adv(p);
    if (!is_word(p, "in")) { p->err = "expected 'in' after case word"; sh_free_node(n); return NULL; }
    adv(p);
    skip_seps(p);
    while (!is_word(p, "esac") && curt(p) != T_EOF) {
        if (curt(p) == T_LPAREN) adv(p);   // optional leading '('
        sh_case_clause cl;
        cl.pats = NULL; cl.npat = 0; cl.body = NULL;
        for (;;) {
            if (curt(p) != T_WORD) {
                p->err = "expected case pattern";
                for (int i = 0; i < cl.npat; i++) free(cl.pats[i]);
                free(cl.pats); sh_free_node(n); return NULL;
            }
            cl.pats = realloc(cl.pats, (cl.npat + 1) * sizeof(char *));
            cl.pats[cl.npat++] = strdup(cur(p)->text);
            adv(p);
            if (curt(p) == T_BAR) { adv(p); continue; }
            break;
        }
        if (curt(p) != T_RPAREN) {
            p->err = "expected ')' in case";
            for (int i = 0; i < cl.npat; i++) free(cl.pats[i]);
            free(cl.pats); sh_free_node(n); return NULL;
        }
        adv(p);
        cl.body = parse_list(p);
        if (!cl.body) {
            for (int i = 0; i < cl.npat; i++) free(cl.pats[i]);
            free(cl.pats); sh_free_node(n); return NULL;
        }
        n->clauses = realloc(n->clauses, (n->nclause_case + 1) * sizeof(sh_case_clause));
        n->clauses[n->nclause_case++] = cl;
        if (curt(p) == T_DSEMI) { adv(p); skip_seps(p); }
        else break;   // last clause may omit ';;'
    }
    if (!is_word(p, "esac")) { p->err = "expected 'esac'"; sh_free_node(n); return NULL; }
    adv(p);
    return n;
}

static node *parse_group(P *p)
{
    adv(p); // '{'
    node *n = new_node(N_GROUP);
    n->subshell = 0;
    n->body = parse_list(p);
    if (!n->body) { sh_free_node(n); return NULL; }
    if (!is_word(p, "}")) { p->err = "expected '}'"; sh_free_node(n); return NULL; }
    adv(p);
    return n;
}

static node *parse_subshell(P *p)
{
    adv(p); // '('
    node *n = new_node(N_GROUP);
    n->subshell = 1;
    n->body = parse_list(p);
    if (!n->body) { sh_free_node(n); return NULL; }
    if (curt(p) != T_RPAREN) { p->err = "expected ')'"; sh_free_node(n); return NULL; }
    adv(p);
    return n;
}

static node *parse_funcdef(P *p)
{
    node *n = new_node(N_FUNCDEF);
    n->func_name = strdup(cur(p)->text);
    adv(p);            // name
    adv(p);            // '('
    if (curt(p) != T_RPAREN) { p->err = "expected ')' in function definition"; sh_free_node(n); return NULL; }
    adv(p);            // ')'
    skip_newlines(p);
    n->body = parse_command(p);
    if (!n->body) { sh_free_node(n); return NULL; }
    return n;
}

static node *parse_command(P *p)
{
    // Simple commands handle their own (possibly interleaved) redirects.
    if (curt(p) != T_LPAREN && !is_word(p, "{") &&
        !(curt(p) == T_WORD && !is_reserved(p) && peekt(p) == T_LPAREN) &&
        !is_word(p, "if") && !is_word(p, "while") && !is_word(p, "until") &&
        !is_word(p, "for") &&
        !is_word(p, "case"))
        return parse_simple(p);

    // Compound command: parse it, then attach any trailing redirect list.
    node *n;
    if (curt(p) == T_LPAREN)      n = parse_subshell(p);
    else if (is_word(p, "{"))     n = parse_group(p);
    else if (is_word(p, "if"))    n = parse_if(p);
    else if (is_word(p, "while") || is_word(p, "until")) n = parse_while(p);
    else if (is_word(p, "for"))   n = parse_for(p);
    else if (is_word(p, "case"))  n = parse_case(p);
    else                          n = parse_funcdef(p);  // WORD '(' ')' body
    if (!n) return NULL;
    if (!parse_redir_list(p, n)) { sh_free_node(n); return NULL; }
    return n;
}

static node *parse_pipeline(P *p)
{
    // A leading `!` (as its own word, at command-word position) negates the
    // pipeline's exit status. `! ! x` toggles, matching bash/dash.
    int negated = 0;
    while (is_word(p, "!")) { negated = !negated; adv(p); skip_newlines(p); }

    node *first = parse_command(p);
    if (!first) return NULL;
    if (curt(p) != T_BAR) { first->negated = negated; return first; }

    node *n = new_node(N_PIPE);
    push_child(n, first);
    while (curt(p) == T_BAR) {
        adv(p);
        skip_newlines(p);
        node *c = parse_command(p);
        if (!c) { sh_free_node(n); return NULL; }
        push_child(n, c);
    }
    n->negated = negated;
    return n;
}

static node *parse_and_or(P *p)
{
    node *left = parse_pipeline(p);
    if (!left) return NULL;
    while (curt(p) == T_AMPAMP || curt(p) == T_BARBAR) {
        int op = curt(p);
        adv(p);
        skip_newlines(p);
        node *right = parse_pipeline(p);
        if (!right) { sh_free_node(left); return NULL; }
        node *n = new_node(N_ANDOR);
        n->left = left;
        n->right = right;
        n->andor_op = op;
        left = n;
    }
    return left;
}

// A list runs until EOF or a reserved terminator word (then/else/elif/fi/do/done).
static node *parse_list(P *p)
{
    node *n = new_node(N_LIST);
    skip_seps(p);
    while (curt(p) != T_EOF && curt(p) != T_RPAREN && curt(p) != T_DSEMI &&
           !is_reserved(p)) {
        node *ao = parse_and_or(p);
        if (!ao) { sh_free_node(n); return NULL; }
        push_child(n, ao);
        // separators between and_or units
        if (curt(p) == T_SEMI || curt(p) == T_NEWLINE) {
            skip_seps(p);
        } else {
            break;  // next token is EOF or a reserved word
        }
    }
    return n;
}

node *sh_parse(sh_toklist *tl, const char **errmsg)
{
    P p = { tl, 0, NULL };
    node *n = parse_list(&p);
    if (!n) { if (errmsg) *errmsg = p.err ? p.err : "parse error"; return NULL; }
    if (curt(&p) != T_EOF) {
        if (errmsg) *errmsg = "unexpected token";
        sh_free_node(n);
        return NULL;
    }
    return n;
}

void sh_free_node(node *n)
{
    if (!n) return;
    for (int i = 0; i < n->nchild; i++) sh_free_node(n->children[i]);
    free(n->children);
    sh_free_node(n->left);
    sh_free_node(n->right);
    for (int i = 0; i < n->nassign; i++) free(n->assigns[i]);
    free(n->assigns);
    for (int i = 0; i < n->nword; i++) free(n->words[i]);
    free(n->words);
    for (int i = 0; i < n->nredir; i++) free(n->redirs[i].word);
    free(n->redirs);
    for (int i = 0; i < n->nclause; i++) { sh_free_node(n->conds[i]); sh_free_node(n->bodies[i]); }
    free(n->conds);
    free(n->bodies);
    sh_free_node(n->else_body);
    sh_free_node(n->cond);
    sh_free_node(n->body);
    free(n->for_name);
    for (int i = 0; i < n->for_nword; i++) free(n->for_words[i]);
    free(n->for_words);
    free(n->case_word);
    for (int i = 0; i < n->nclause_case; i++) {
        for (int j = 0; j < n->clauses[i].npat; j++) free(n->clauses[i].pats[j]);
        free(n->clauses[i].pats);
        sh_free_node(n->clauses[i].body);
    }
    free(n->clauses);
    free(n->func_name);
    free(n);
}
