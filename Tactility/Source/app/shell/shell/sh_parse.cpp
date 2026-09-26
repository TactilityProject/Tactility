// Shell grammar parser: token list -> AST.
//
// A push-down parser: each grammar rule is a frame on an explicit stack, and a
// rule that needs a nested rule (a compound command's body, a pipeline's next
// command, ...) pushes it and resumes when it completes. Nesting depth is
// bounded by SH_MAX_PARSE_FRAMES instead of the native stack.
#include <Tactility/app/shell/shell/sh_parse.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#define SH_MAX_PARSE_FRAMES 1024

namespace {

struct NodeDeleter {
    void operator()(node* n) const { sh_free_node(n); }
};
using NodePtr = std::unique_ptr<node, NodeDeleter>;

node* new_node(node_kind k)
{
    auto* n = static_cast<node*>(calloc(1, sizeof(node)));
    n->kind = k;
    return n;
}

void push_child(node* n, node* c)
{
    n->children = static_cast<node**>(realloc(n->children, (n->nchild + 1) * sizeof(node*)));
    n->children[n->nchild++] = c;
}

void push_clause(node* n, node* cond, node* body)
{
    n->conds = static_cast<node**>(realloc(n->conds, (n->nclause + 1) * sizeof(node*)));
    n->bodies = static_cast<node**>(realloc(n->bodies, (n->nclause + 1) * sizeof(node*)));
    n->conds[n->nclause] = cond;
    n->bodies[n->nclause] = body;
    n->nclause++;
}

int is_redir_tok(tok_type t)
{
    return t == T_LT || t == T_GT || t == T_GTGT || t == T_GTAMP ||
           t == T_LTAMP || t == T_CLOBBER || t == T_DLESS || t == T_DLESSDASH;
}

sh_redir* push_redir(node* n)
{
    n->redirs = static_cast<sh_redir*>(realloc(n->redirs, (n->nredir + 1) * sizeof(sh_redir)));
    sh_redir* r = &n->redirs[n->nredir++];
    r->kind = R_OUT; r->fd = 1; r->dupfd = 0; r->word = nullptr; r->heredoc_quoted = 0;
    return r;
}

int all_digits(const char* s)
{
    if (!*s) return 0;
    for (; *s; s++) if (*s < '0' || *s > '9') return 0;
    return 1;
}

int looks_like_assign(const char* w)
{
    if (!(*w == '_' || (*w >= 'A' && *w <= 'Z') || (*w >= 'a' && *w <= 'z'))) return 0;
    const char* q = w + 1;
    while (*q && *q != '=') {
        if (!(*q == '_' || (*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') ||
              (*q >= '0' && *q <= '9'))) return 0;
        q++;
    }
    return *q == '=';
}

class Parser;

enum class Step { Call, Return, Fail };

// One frame per grammar rule. step() is called on first entry with a null
// `child`, and again after each nested rule it pushed has completed, with that
// rule's result.
struct ParseList { NodePtr n; bool started = false; Step step(Parser& p, NodePtr child); };
struct ParseAndOr { NodePtr left; int op = 0; enum { Start, Left, Right } phase = Start; Step step(Parser& p, NodePtr child); };
struct ParsePipeline { NodePtr pipe; int negated = 0; enum { Start, First, Next } phase = Start; Step step(Parser& p, NodePtr child); };
struct ParseCommand { bool started = false; Step step(Parser& p, NodePtr child); };
struct ParseGroup { NodePtr n; bool subshell; bool started = false; Step step(Parser& p, NodePtr child); };
struct ParseFuncdef { NodePtr n; bool started = false; Step step(Parser& p, NodePtr child); };
struct ParseIf { NodePtr n; NodePtr cond; enum { Start, Cond, Body, Else } phase = Start; Step step(Parser& p, NodePtr child); };
struct ParseWhile { NodePtr n; enum { Start, Cond, Body } phase = Start; Step step(Parser& p, NodePtr child); };
struct ParseFor { NodePtr n; bool started = false; Step step(Parser& p, NodePtr child); };

struct ParseCase {
    NodePtr n;
    std::vector<char*> pats;   // patterns of the clause whose body is being parsed
    bool started = false;

    ParseCase() = default;
    ParseCase(ParseCase&&) = default;
    ~ParseCase() { for (char* pat : pats) free(pat); }

    Step step(Parser& p, NodePtr child);
};

using ParseFrame = std::variant<ParseList, ParseAndOr, ParsePipeline, ParseCommand, ParseGroup,
                                ParseFuncdef, ParseIf, ParseWhile, ParseFor, ParseCase>;

class Parser {
public:
    explicit Parser(sh_toklist* tl) : tl(tl) {}

    sh_toklist* tl;
    int pos = 0;
    const char* err = nullptr;

    sh_tok* cur() { return &tl->toks[pos]; }
    tok_type curt() { return tl->toks[pos].type; }
    tok_type peekt() { return tl->toks[pos].type == T_EOF ? T_EOF : tl->toks[pos + 1].type; }
    void adv() { if (tl->toks[pos].type != T_EOF) pos++; }

    // Skip newlines/semicolons acting as blank separators.
    void skip_seps() { while (curt() == T_NEWLINE || curt() == T_SEMI) adv(); }
    void skip_newlines() { while (curt() == T_NEWLINE) adv(); }

    // Is the current token a bare word equal to `kw`?
    bool is_word(const char* kw)
    {
        sh_tok* t = cur();
        return t->type == T_WORD && t->text && strcmp(t->text, kw) == 0;
    }

    bool is_reserved()
    {
        static const char* kw[] = { "then", "else", "elif", "fi", "do", "done", "esac", "}", nullptr };
        if (curt() != T_WORD) return false;
        for (int i = 0; kw[i]; i++) if (is_word(kw[i])) return true;
        return false;
    }

    Step fail(const char* message)
    {
        err = message;
        return Step::Fail;
    }

    Step call(ParseFrame frame)
    {
        pending.emplace(std::move(frame));
        return Step::Call;
    }

    Step finish(NodePtr n)
    {
        result = std::move(n);
        return Step::Return;
    }

    node* run()
    {
        std::vector<ParseFrame> frames;
        frames.emplace_back(ParseList {});
        NodePtr child;
        while (!frames.empty()) {
            Step step = std::visit([&](auto& frame) { return frame.step(*this, std::move(child)); }, frames.back());
            switch (step) {
                case Step::Call:
                    if (frames.size() >= SH_MAX_PARSE_FRAMES) {
                        err = "nesting too deep";
                        return nullptr;
                    }
                    frames.push_back(std::move(*pending));
                    pending.reset();
                    break;
                case Step::Return:
                    frames.pop_back();
                    child = std::move(result);
                    break;
                case Step::Fail:
                    return nullptr;   // frames' partial nodes are freed as the stack unwinds
            }
        }
        return child.release();
    }

    // Parse the redirection at the current token onto `n`. Returns 1 on success,
    // 0 on error (err set).
    int parse_redir(node* n)
    {
        tok_type t = curt();
        int fd = cur()->rfd;
        // No explicit fd digit (rfd == -1): default to stdin for input operators,
        // stdout for output operators.
        if (fd < 0) {
            fd = (t == T_LT || t == T_LTAMP || t == T_DLESS || t == T_DLESSDASH) ? 0 : 1;
        }

        if (t == T_DLESS || t == T_DLESSDASH) {
            char* body = cur()->text ? strdup(cur()->text) : strdup("");
            int quoted = cur()->hd_quoted;
            adv();
            if (curt() != T_WORD) { free(body); err = "expected here-doc delimiter"; return 0; }
            sh_redir* r = push_redir(n);
            r->kind = R_HEREDOC; r->fd = 0; r->word = body; r->heredoc_quoted = quoted;
            adv();
            return 1;
        }

        if (t == T_GTAMP || t == T_LTAMP) {
            adv();
            if (curt() != T_WORD) { err = "expected fd or filename after >&"; return 0; }
            const char* tgt = cur()->text;
            sh_redir* r = push_redir(n);
            r->fd = fd;
            if (all_digits(tgt)) { r->kind = R_DUP; r->dupfd = static_cast<int>(strtol(tgt, nullptr, 10)); }
            else if (strcmp(tgt, "-") == 0) { r->kind = R_CLOSE; }
            else { r->kind = (t == T_GTAMP) ? R_OUT : R_IN; r->word = strdup(tgt); }
            adv();
            return 1;
        }

        // T_LT / T_GT / T_GTGT / T_CLOBBER: filename target.
        redir_kind rk = (t == T_LT) ? R_IN : (t == T_GTGT) ? R_APPEND : R_OUT;
        adv();
        if (curt() != T_WORD) { err = "expected filename after redirection"; return 0; }
        sh_redir* r = push_redir(n);
        r->kind = rk; r->fd = fd; r->word = strdup(cur()->text);
        adv();
        return 1;
    }

    // Consume any trailing redirections onto a compound command. Returns 1/0.
    int parse_redir_list(node* n)
    {
        while (is_redir_tok(curt())) {
            if (!parse_redir(n)) return 0;
        }
        return 1;
    }

    NodePtr parse_simple()
    {
        NodePtr n(new_node(N_SIMPLE));
        int seen_word = 0;

        for (;;) {
            tok_type t = curt();
            if (t == T_WORD) {
                if (!seen_word && looks_like_assign(cur()->text)) {
                    n->assigns = static_cast<char**>(realloc(n->assigns, (n->nassign + 1) * sizeof(char*)));
                    n->assigns[n->nassign++] = strdup(cur()->text);
                    adv();
                    continue;
                }
                // Reserved words are only special in command position (first word);
                // as arguments they are ordinary words (e.g. `echo done`).
                if (!seen_word && is_reserved()) break;
                n->words = static_cast<char**>(realloc(n->words, (n->nword + 1) * sizeof(char*)));
                n->words[n->nword++] = strdup(cur()->text);
                seen_word = 1;
                adv();
            } else if (is_redir_tok(t)) {
                // Redirects may appear before, between, or after words and do not
                // count as the command word (so `FOO=x >f BAR=y cmd` still assigns).
                if (!parse_redir(n.get())) return nullptr;
            } else {
                break;
            }
        }
        if (n->nword == 0 && n->nassign == 0 && n->nredir == 0) {
            err = "expected command";
            return nullptr;
        }
        return n;
    }

private:
    std::optional<ParseFrame> pending;
    NodePtr result;
};

// A list runs until EOF or a reserved terminator word (then/else/elif/fi/do/done).
Step ParseList::step(Parser& p, NodePtr child)
{
    if (!started) {
        started = true;
        n.reset(new_node(N_LIST));
        p.skip_seps();
    } else {
        push_child(n.get(), child.release());
        // separators between and_or units
        if (p.curt() == T_SEMI || p.curt() == T_NEWLINE) {
            p.skip_seps();
        } else {
            return p.finish(std::move(n));   // next token is EOF or a reserved word
        }
    }
    if (p.curt() != T_EOF && p.curt() != T_RPAREN && p.curt() != T_DSEMI && !p.is_reserved()) {
        return p.call(ParseAndOr {});
    }
    return p.finish(std::move(n));
}

Step ParseAndOr::step(Parser& p, NodePtr child)
{
    switch (phase) {
        case Start:
            phase = Left;
            return p.call(ParsePipeline {});
        case Left:
            left = std::move(child);
            break;
        case Right: {
            node* n = new_node(N_ANDOR);
            n->left = left.release();
            n->right = child.release();
            n->andor_op = op;
            left.reset(n);
            break;
        }
    }
    if (p.curt() == T_AMPAMP || p.curt() == T_BARBAR) {
        op = p.curt();
        p.adv();
        p.skip_newlines();
        phase = Right;
        return p.call(ParsePipeline {});
    }
    return p.finish(std::move(left));
}

Step ParsePipeline::step(Parser& p, NodePtr child)
{
    switch (phase) {
        case Start:
            // A leading `!` (as its own word, at command-word position) negates the
            // pipeline's exit status. `! ! x` toggles, matching bash/dash.
            while (p.is_word("!")) { negated = !negated; p.adv(); p.skip_newlines(); }
            phase = First;
            return p.call(ParseCommand {});
        case First:
            if (p.curt() != T_BAR) {
                child->negated = negated;
                return p.finish(std::move(child));
            }
            pipe.reset(new_node(N_PIPE));
            push_child(pipe.get(), child.release());
            break;
        case Next:
            push_child(pipe.get(), child.release());
            break;
    }
    if (p.curt() == T_BAR) {
        p.adv();
        p.skip_newlines();
        phase = Next;
        return p.call(ParseCommand {});
    }
    pipe->negated = negated;
    return p.finish(std::move(pipe));
}

Step ParseCommand::step(Parser& p, NodePtr child)
{
    if (started) {
        // Compound command parsed: attach any trailing redirect list.
        if (!p.parse_redir_list(child.get())) return Step::Fail;
        return p.finish(std::move(child));
    }
    started = true;

    // Simple commands handle their own (possibly interleaved) redirects.
    if (p.curt() != T_LPAREN && !p.is_word("{") &&
        !(p.curt() == T_WORD && !p.is_reserved() && p.peekt() == T_LPAREN) &&
        !p.is_word("if") && !p.is_word("while") && !p.is_word("until") &&
        !p.is_word("for") &&
        !p.is_word("case")) {
        NodePtr n = p.parse_simple();
        if (!n) return Step::Fail;
        return p.finish(std::move(n));
    }

    if (p.curt() == T_LPAREN) return p.call(ParseGroup { .subshell = true });
    if (p.is_word("{")) return p.call(ParseGroup { .subshell = false });
    if (p.is_word("if")) return p.call(ParseIf {});
    if (p.is_word("while") || p.is_word("until")) return p.call(ParseWhile {});
    if (p.is_word("for")) return p.call(ParseFor {});
    if (p.is_word("case")) return p.call(ParseCase {});
    return p.call(ParseFuncdef {});   // WORD '(' ')' body
}

// { list; } or ( list )
Step ParseGroup::step(Parser& p, NodePtr child)
{
    if (!started) {
        started = true;
        p.adv(); // '{' or '('
        n.reset(new_node(N_GROUP));
        n->subshell = subshell ? 1 : 0;
        return p.call(ParseList {});
    }
    n->body = child.release();
    if (subshell) {
        if (p.curt() != T_RPAREN) return p.fail("expected ')'");
    } else {
        if (!p.is_word("}")) return p.fail("expected '}'");
    }
    p.adv();
    return p.finish(std::move(n));
}

Step ParseFuncdef::step(Parser& p, NodePtr child)
{
    if (!started) {
        started = true;
        n.reset(new_node(N_FUNCDEF));
        n->func_name = strdup(p.cur()->text);
        p.adv();            // name
        p.adv();            // '('
        if (p.curt() != T_RPAREN) return p.fail("expected ')' in function definition");
        p.adv();            // ')'
        p.skip_newlines();
        return p.call(ParseCommand {});
    }
    n->body = child.release();
    return p.finish(std::move(n));
}

Step ParseIf::step(Parser& p, NodePtr child)
{
    switch (phase) {
        case Start:
            p.adv(); // 'if'
            n.reset(new_node(N_IF));
            phase = Cond;
            return p.call(ParseList {});
        case Cond:
            cond = std::move(child);
            if (!p.is_word("then")) return p.fail("expected 'then'");
            p.adv();
            phase = Body;
            return p.call(ParseList {});
        case Body:
            if (child->nchild == 0) return p.fail("empty then body");
            push_clause(n.get(), cond.release(), child.release());
            if (p.is_word("elif")) {
                p.adv();
                phase = Cond;
                return p.call(ParseList {});
            }
            if (p.is_word("else")) {
                p.adv();
                phase = Else;
                return p.call(ParseList {});
            }
            break;
        case Else:
            n->else_body = child.release();
            if (n->else_body->nchild == 0) return p.fail("empty else body");
            break;
    }
    if (!p.is_word("fi")) return p.fail("expected 'fi'");
    p.adv();
    return p.finish(std::move(n));
}

Step ParseWhile::step(Parser& p, NodePtr child)
{
    switch (phase) {
        case Start: {
            int until = p.is_word("until");
            p.adv(); // 'while' / 'until'
            n.reset(new_node(N_WHILE));
            n->until = until;
            phase = Cond;
            return p.call(ParseList {});
        }
        case Cond:
            n->cond = child.release();
            if (!p.is_word("do")) return p.fail("expected 'do'");
            p.adv();
            phase = Body;
            return p.call(ParseList {});
        case Body:
            break;
    }
    n->body = child.release();
    if (n->body->nchild == 0) return p.fail("empty do/done body");
    if (!p.is_word("done")) return p.fail("expected 'done'");
    p.adv();
    return p.finish(std::move(n));
}

// Collect the raw words after `for NAME in` up to a separator.
Step ParseFor::step(Parser& p, NodePtr child)
{
    if (started) {
        n->body = child.release();
        if (n->body->nchild == 0) return p.fail("empty do/done body");
        if (!p.is_word("done")) return p.fail("expected 'done'");
        p.adv();
        return p.finish(std::move(n));
    }
    started = true;

    p.adv(); // 'for'
    if (p.curt() != T_WORD) return p.fail("expected name after for");
    // The loop variable must be a valid name (dash rejects e.g. `for -`).
    const char* nm = p.cur()->text;
    if (!(nm[0] == '_' || (nm[0] >= 'a' && nm[0] <= 'z') || (nm[0] >= 'A' && nm[0] <= 'Z'))) {
        return p.fail("bad for loop variable");
    }
    for (const char* q = nm + 1; *q; q++) {
        if (!(*q == '_' || (*q >= '0' && *q <= '9') ||
              (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z'))) {
            return p.fail("bad for loop variable");
        }
    }
    n.reset(new_node(N_FOR));
    n->for_name = strdup(nm);
    p.adv();
    p.skip_newlines();   // `for i <newline> in ...` is legal (`for i; in` is not)
    if (p.is_word("in")) {
        p.adv();
        while (p.curt() == T_WORD && !p.is_reserved()) {
            n->for_words = static_cast<char**>(realloc(n->for_words, (n->for_nword + 1) * sizeof(char*)));
            n->for_words[n->for_nword++] = strdup(p.cur()->text);
            p.adv();
        }
    } else {
        // `for x; do ...` with no `in`: iterate over the positional params.
        n->for_implicit = 1;
    }
    p.skip_seps();
    if (!p.is_word("do")) return p.fail("expected 'do'");
    p.adv();
    return p.call(ParseList {});
}

// case WORD in [(] pat [| pat]... ) list ;; ... esac
Step ParseCase::step(Parser& p, NodePtr child)
{
    if (!started) {
        started = true;
        p.adv(); // 'case'
        if (p.curt() != T_WORD) return p.fail("expected word after case");
        n.reset(new_node(N_CASE));
        n->case_word = strdup(p.cur()->text);
        p.adv();
        if (!p.is_word("in")) return p.fail("expected 'in' after case word");
        p.adv();
        p.skip_seps();
    } else {
        sh_case_clause cl;
        cl.npat = static_cast<int>(pats.size());
        cl.pats = static_cast<char**>(malloc(pats.size() * sizeof(char*)));
        memcpy(cl.pats, pats.data(), pats.size() * sizeof(char*));
        pats.clear();
        cl.body = child.release();
        n->clauses = static_cast<sh_case_clause*>(realloc(n->clauses, (n->nclause_case + 1) * sizeof(sh_case_clause)));
        n->clauses[n->nclause_case++] = cl;
        if (p.curt() == T_DSEMI) {
            p.adv();
            p.skip_seps();
        } else {
            // last clause may omit ';;'
            if (!p.is_word("esac")) return p.fail("expected 'esac'");
            p.adv();
            return p.finish(std::move(n));
        }
    }

    if (p.is_word("esac") || p.curt() == T_EOF) {
        if (!p.is_word("esac")) return p.fail("expected 'esac'");
        p.adv();
        return p.finish(std::move(n));
    }

    if (p.curt() == T_LPAREN) p.adv();   // optional leading '('
    for (;;) {
        if (p.curt() != T_WORD) return p.fail("expected case pattern");
        pats.push_back(strdup(p.cur()->text));
        p.adv();
        if (p.curt() == T_BAR) { p.adv(); continue; }
        break;
    }
    if (p.curt() != T_RPAREN) return p.fail("expected ')' in case");
    p.adv();
    return p.call(ParseList {});
}

} // namespace

extern "C" {

node* sh_parse(sh_toklist* tl, const char** errmsg)
{
    Parser p(tl);
    node* n = p.run();
    if (!n) {
        if (errmsg) *errmsg = p.err ? p.err : "parse error";
        return nullptr;
    }
    if (p.curt() != T_EOF) {
        if (errmsg) *errmsg = "unexpected token";
        sh_free_node(n);
        return nullptr;
    }
    return n;
}

void sh_free_node(node* root)
{
    // A worklist keeps freeing deeply nested trees off the native stack.
    std::vector<node*> pending;
    if (root) pending.push_back(root);
    while (!pending.empty()) {
        node* n = pending.back();
        pending.pop_back();
        auto later = [&pending](node* c) { if (c) pending.push_back(c); };

        for (int i = 0; i < n->nchild; i++) later(n->children[i]);
        free(n->children);
        later(n->left);
        later(n->right);
        for (int i = 0; i < n->nassign; i++) free(n->assigns[i]);
        free(n->assigns);
        for (int i = 0; i < n->nword; i++) free(n->words[i]);
        free(n->words);
        for (int i = 0; i < n->nredir; i++) free(n->redirs[i].word);
        free(n->redirs);
        for (int i = 0; i < n->nclause; i++) { later(n->conds[i]); later(n->bodies[i]); }
        free(n->conds);
        free(n->bodies);
        later(n->else_body);
        later(n->cond);
        later(n->body);
        free(n->for_name);
        for (int i = 0; i < n->for_nword; i++) free(n->for_words[i]);
        free(n->for_words);
        free(n->case_word);
        for (int i = 0; i < n->nclause_case; i++) {
            for (int j = 0; j < n->clauses[i].npat; j++) free(n->clauses[i].pats[j]);
            free(n->clauses[i].pats);
            later(n->clauses[i].body);
        }
        free(n->clauses);
        free(n->func_name);
        free(n);
    }
}

}
