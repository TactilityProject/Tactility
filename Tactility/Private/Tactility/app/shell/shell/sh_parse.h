#pragma once
#include "sh_lex.h"

typedef enum {
    N_LIST,     // sequence of and_or nodes (children), run in order
    N_ANDOR,    // binary: left <op> right, op in { &&, || }
    N_PIPE,     // children[] connected by pipes (temp-file semantics)
    N_SIMPLE,   // assignments + argv words + redirects
    N_IF,       // if/elif/else chain
    N_WHILE,
    N_FOR,
    N_CASE,     // case ... esac
    N_GROUP,    // { list; } or ( list ) subshell
    N_FUNCDEF   // name() compound-command
} node_kind;

typedef enum { R_IN, R_OUT, R_APPEND, R_HEREDOC, R_DUP, R_CLOSE } redir_kind;

typedef struct {
    redir_kind kind;
    int fd;              // source fd (0 for <, 1 for >, or explicit like 2>)
    int dupfd;           // R_DUP: target fd (the n in `fd>&n`)
    char *word;          // raw filename word (owned); for R_HEREDOC: the body
    int heredoc_quoted;  // R_HEREDOC: body must not be expanded
} sh_redir;

// One case clause: patterns (raw words) plus the body list.
typedef struct {
    char **pats;
    int npat;
    struct node *body;
} sh_case_clause;

typedef struct node {
    node_kind kind;
    int negated;   // leading `!` inverts this pipeline's exit status

    // N_LIST / N_PIPE: children
    struct node **children;
    int nchild;

    // N_ANDOR
    struct node *left;
    struct node *right;
    int andor_op;   // T_AMPAMP or T_BARBAR

    // N_SIMPLE
    char **assigns;   // "NAME=raw" strings (owned)
    int nassign;
    char **words;     // raw argv words (owned)
    int nword;
    sh_redir *redirs;
    int nredir;

    // N_IF: parallel arrays of conditions/bodies; else_body optional
    struct node **conds;
    struct node **bodies;
    int nclause;
    struct node *else_body;

    // N_WHILE / N_FOR body
    struct node *cond;   // while condition
    int until;           // N_WHILE: 1 = `until` (loop while cond is non-zero)
    struct node *body;

    // N_FOR
    char *for_name;
    char **for_words;
    int for_nword;
    int for_implicit;    // `for x; do` (no `in`): iterate over "$@"

    // N_CASE
    char *case_word;           // raw subject word
    sh_case_clause *clauses;
    int nclause_case;

    // N_GROUP
    int subshell;              // 0 = { }, 1 = ( )

    // N_FUNCDEF (body reuses `body`)
    char *func_name;
} node;

// Parse a token list into an AST (N_LIST). Returns NULL on parse error and
// sets *errmsg to a static description.
node *sh_parse(sh_toklist *tl, const char **errmsg);
void  sh_free_node(node *n);
