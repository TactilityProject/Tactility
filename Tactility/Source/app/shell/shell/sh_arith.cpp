// POSIX arithmetic expansion `$(( ))` evaluator.
//
// Operator-precedence evaluation over explicit operator/value/input stacks,
// operating on signed `long` only. No floats, no arrays, no `**`/`++`/`--`
// (dash rejects those; so do we), and -- by scope decision -- no ternary,
// bitwise, or shift operators. Bare variable names auto-deref;
// unset/empty is 0; assignment operators write back to shell variables.
//
// A variable's value is itself an arithmetic expression. It is evaluated by
// pushing it as a nested input rather than by recursing, so neither deeply
// parenthesized expressions nor chains of variables grow the native stack.
#include <Tactility/app/shell/shell/sh_arith.h>

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int ARITH_MAX_DEPTH = 32;   // guard for variable-value re-evaluation

enum class Op {
    Comma, Assign, Or, And, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    Add, Subtract, Multiply, Divide, Modulo, Plus, Negate, Not,
    // Markers: not operators, never reduced
    LeftParen,   // '('
    Deref,       // boundary of a variable value being evaluated
    AssignDone   // boundary of a compound assignment's current value being evaluated
};

struct Operator {
    Op op;
    char assign = 0;     // Assign/AssignDone: compound operation char, 0 for plain `=`
    std::string name;    // Assign/AssignDone: target variable
    long rhs = 0;        // AssignDone: the already-evaluated right-hand side
};

struct Input {
    std::string text;
    size_t pos = 0;
    int depth = 0;       // 0 for the expression itself, >0 for variable values

    char at(size_t offset = 0) const { return pos + offset < text.size() ? text[pos + offset] : '\0'; }
    bool startsWith(const char* token) const { return text.compare(pos, std::char_traits<char>::length(token), token) == 0; }
    void skipWhitespace() { while (at() == ' ' || at() == '\t' || at() == '\n') pos++; }
};

bool isMarker(Op op) {
    return op == Op::LeftParen || op == Op::Deref || op == Op::AssignDone;
}

bool isUnary(Op op) {
    return op == Op::Plus || op == Op::Negate || op == Op::Not;
}

int precedence(Op op) {
    switch (op) {
        case Op::Comma: return 1;
        case Op::Assign: return 2;
        case Op::Or: return 3;
        case Op::And: return 4;
        case Op::Equal: case Op::NotEqual: return 5;
        case Op::Less: case Op::LessEqual: case Op::Greater: case Op::GreaterEqual: return 6;
        case Op::Add: case Op::Subtract: return 7;
        case Op::Multiply: case Op::Divide: case Op::Modulo: return 8;
        default: return 9;   // unary
    }
}

class Evaluator {
public:
    Evaluator(sh_state* state, const char* expr) : st(state) {
        inputs.push_back(Input { expr, 0, 0 });
    }

    int run(long* out, const char** errmsg) {
        // An empty or all-whitespace expression evaluates to 0 (dash: `$(( ))`).
        inputs.back().skipWhitespace();
        if (inputs.back().at() == '\0') {
            if (errmsg) *errmsg = nullptr;
            *out = 0;
            return 0;
        }

        while (error == nullptr && step()) {}

        if (error != nullptr) {
            if (errmsg) *errmsg = error;
            return 1;
        }
        if (errmsg) *errmsg = nullptr;
        *out = values.back();
        return 0;
    }

private:
    sh_state* st;
    std::vector<Input> inputs;
    std::vector<Operator> ops;
    std::vector<long> values;
    const char* error = nullptr;
    bool expectOperand = true;
    // An assignment may only start an assignment-expression: at the very start, or after '(', ',' or another assignment.
    bool assignAllowed = true;

    void fail(const char* message) {
        if (error == nullptr) error = message;
    }

    long popValue() {
        long value = values.back();
        values.pop_back();
        return value;
    }

    /** Performs one unit of work. Returns false when evaluation is complete. */
    bool step() {
        Input& in = inputs.back();
        in.skipWhitespace();
        if (expectOperand) {
            readOperand(in);
            return true;
        }
        if (in.at() == '\0') {
            return endOfInput();
        }
        readOperator(in);
        return true;
    }

    void readOperand(Input& in) {
        char c = in.at();
        if (c == '(') { in.pos++; ops.push_back({ Op::LeftParen }); assignAllowed = true; return; }
        if (c == '+') { in.pos++; ops.push_back({ Op::Plus }); assignAllowed = false; return; }
        if (c == '-') { in.pos++; ops.push_back({ Op::Negate }); assignAllowed = false; return; }
        if (c == '!') { in.pos++; ops.push_back({ Op::Not }); assignAllowed = false; return; }

        if (assignAllowed && tryAssignment(in)) {
            return;
        }

        // A leading '$' before an identifier: normal expansion would have handled
        // $var already, but if it reaches here, skip the '$' and deref the name.
        if (in.at() == '$') in.pos++;

        if (isdigit(static_cast<unsigned char>(in.at()))) {
            long value = readNumber(in);
            if (error == nullptr) pushOperand(value);
            return;
        }

        if (isalpha(static_cast<unsigned char>(in.at())) || in.at() == '_') {
            std::string name = readIdentifier(in);
            deref(name, [this](long value) { pushOperand(value); }, Operator { Op::Deref });
            return;
        }

        fail("unexpected token in arithmetic");
    }

    void pushOperand(long value) {
        values.push_back(value);
        expectOperand = false;
        assignAllowed = false;
    }

    /**
     * Resolves a variable's arithmetic value. An unset or blank value is available immediately and
     * passed to onImmediate. Otherwise its text is pushed as a nested input behind `marker`, and the
     * result arrives through endOfInput().
     */
    template <typename OnImmediate>
    void deref(const std::string& name, OnImmediate&& onImmediate, Operator marker) {
        const char* value = sh_get(st, name.c_str());
        if (value != nullptr) {
            while (*value == ' ' || *value == '\t' || *value == '\n') value++;   // empty/blank value -> 0
        }
        if (value == nullptr || *value == '\0') {
            onImmediate(0);
            return;
        }
        int depth = inputs.back().depth + 1;
        if (depth > ARITH_MAX_DEPTH) {
            fail("arithmetic recursion too deep");
            return;
        }
        ops.push_back(std::move(marker));
        // Copied: evaluating the value may reassign the variable it came from.
        inputs.push_back(Input { value, 0, depth });
        expectOperand = true;
        assignAllowed = true;
    }

    // Detect `<ident> <assign-op>` at the current position. On match, pushes the
    // assignment and returns true. Otherwise leaves the position untouched.
    bool tryAssignment(Input& in) {
        size_t save = in.pos;
        if (in.at() == '$') in.pos++;
        if (!(isalpha(static_cast<unsigned char>(in.at())) || in.at() == '_')) { in.pos = save; return false; }
        std::string name = readIdentifier(in);
        in.skipWhitespace();
        static const char* compound[] = { "+=", "-=", "*=", "/=", "%=" };
        for (const char* op : compound) {
            if (in.startsWith(op)) {
                in.pos += 2;
                ops.push_back({ Op::Assign, op[0], std::move(name) });
                return true;
            }
        }
        // plain '=' but not '=='
        if (in.at() == '=' && in.at(1) != '=') {
            in.pos++;
            ops.push_back({ Op::Assign, 0, std::move(name) });
            return true;
        }
        in.pos = save;
        return false;
    }

    static std::string readIdentifier(Input& in) {
        size_t start = in.pos;
        while (isalnum(static_cast<unsigned char>(in.at())) || in.at() == '_') in.pos++;
        return in.text.substr(start, in.pos - start);
    }

    long readNumber(Input& in) {
        long value = 0;
        if (in.at() == '0' && (in.at(1) == 'x' || in.at(1) == 'X')) {
            in.pos += 2;
            if (!isxdigit(static_cast<unsigned char>(in.at()))) { fail("bad hex constant"); return 0; }
            while (isxdigit(static_cast<unsigned char>(in.at()))) {
                char c = in.at();
                int digit = isdigit(static_cast<unsigned char>(c)) ? c - '0' : (tolower(static_cast<unsigned char>(c)) - 'a' + 10);
                value = value * 16 + digit;
                in.pos++;
            }
        } else if (in.at() == '0' && isdigit(static_cast<unsigned char>(in.at(1)))) {
            in.pos++;   // octal
            while (in.at() >= '0' && in.at() <= '7') { value = value * 8 + (in.at() - '0'); in.pos++; }
            if (in.at() == '8' || in.at() == '9') { fail("bad octal constant"); return 0; }
        } else {
            while (isdigit(static_cast<unsigned char>(in.at()))) { value = value * 10 + (in.at() - '0'); in.pos++; }
        }
        return value;
    }

    const char* trailingError() const {
        return inputs.back().depth > 0 ? "invalid arithmetic value" : "unexpected trailing characters in arithmetic";
    }

    void readOperator(Input& in) {
        Op op;
        size_t length = 1;
        char c = in.at();
        if (c == ')') {
            if (!ops.empty() && !isMarker(ops.back().op)) { reduce(); return; }
            if (ops.empty() || ops.back().op != Op::LeftParen) { fail(trailingError()); return; }
            ops.pop_back();
            in.pos++;
            return;
        }
        if (c == ',') op = Op::Comma;
        else if (in.startsWith("||")) { op = Op::Or; length = 2; }
        else if (in.startsWith("&&")) { op = Op::And; length = 2; }
        else if (in.startsWith("==")) { op = Op::Equal; length = 2; }
        else if (in.startsWith("!=")) { op = Op::NotEqual; length = 2; }
        else if (in.startsWith("<=")) { op = Op::LessEqual; length = 2; }
        else if (in.startsWith(">=")) { op = Op::GreaterEqual; length = 2; }
        else if (c == '<') op = Op::Less;
        else if (c == '>') op = Op::Greater;
        else if (in.startsWith("**")) { fail("** not supported"); return; }
        else if (c == '*') op = Op::Multiply;
        else if (c == '/') op = Op::Divide;
        else if (c == '%') op = Op::Modulo;
        else if (in.startsWith("++") || in.startsWith("--")) { fail("++/-- not supported"); return; }
        else if (c == '+') op = Op::Add;
        else if (c == '-') op = Op::Subtract;
        else { fail(trailingError()); return; }

        // Left-associative: finish everything of equal or higher precedence first.
        if (!ops.empty() && !isMarker(ops.back().op) && precedence(ops.back().op) >= precedence(op)) {
            reduce();
            return;
        }
        in.pos += length;
        ops.push_back({ op });
        expectOperand = true;
        assignAllowed = op == Op::Comma;
    }

    /** Called at the end of the current input. Returns false once the whole expression is done. */
    bool endOfInput() {
        if (!ops.empty() && !isMarker(ops.back().op)) {
            reduce();
            return true;
        }
        if (!ops.empty() && ops.back().op == Op::LeftParen) {
            fail("missing ) in arithmetic");
            return true;
        }
        if (inputs.size() == 1) {
            return false;
        }

        Operator marker = std::move(ops.back());
        ops.pop_back();
        inputs.pop_back();
        if (marker.op == Op::AssignDone) {
            long current = popValue();
            finishAssignment(marker.name, marker.assign, current, marker.rhs);
        } else {
            // Op::Deref: the value is already on the value stack
            expectOperand = false;
            assignAllowed = false;
        }
        return true;
    }

    long apply(char op, long current, long rhs) {
        switch (op) {
            case 0:   return rhs;
            case '+': return current + rhs;
            case '-': return current - rhs;
            case '*': return current * rhs;
            case '/': if (rhs == 0) { fail("division by zero"); return 0; } return current / rhs;
            case '%': if (rhs == 0) { fail("division by zero"); return 0; } return current % rhs;
            default:  return rhs;
        }
    }

    void finishAssignment(const std::string& name, char op, long current, long rhs) {
        long result = apply(op, current, rhs);
        if (error != nullptr) return;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%ld", result);
        sh_set(st, name.c_str(), buffer);
        pushOperand(result);
    }

    /** Applies the operator on top of the stack. */
    void reduce() {
        Operator top = std::move(ops.back());
        ops.pop_back();

        if (isUnary(top.op)) {
            long value = popValue();
            values.push_back(top.op == Op::Negate ? -value : top.op == Op::Not ? !value : value);
            return;
        }

        if (top.op == Op::Assign) {
            long rhs = popValue();
            if (top.assign == 0) {
                finishAssignment(top.name, 0, 0, rhs);
                return;
            }
            // Compound: the variable's current value is read after the right-hand side, like dash.
            char assign = top.assign;
            std::string name = top.name;
            deref(
                name,
                [&](long current) { finishAssignment(name, assign, current, rhs); },
                Operator { Op::AssignDone, assign, name, rhs }
            );
            return;
        }

        long rhs = popValue();
        long lhs = popValue();
        long result = 0;
        switch (top.op) {
            case Op::Comma: result = rhs; break;
            case Op::Or: result = (lhs || rhs); break;
            case Op::And: result = (lhs && rhs); break;
            case Op::Equal: result = (lhs == rhs); break;
            case Op::NotEqual: result = (lhs != rhs); break;
            case Op::Less: result = (lhs < rhs); break;
            case Op::LessEqual: result = (lhs <= rhs); break;
            case Op::Greater: result = (lhs > rhs); break;
            case Op::GreaterEqual: result = (lhs >= rhs); break;
            case Op::Add: result = lhs + rhs; break;
            case Op::Subtract: result = lhs - rhs; break;
            case Op::Multiply: result = lhs * rhs; break;
            case Op::Divide:
                if (rhs == 0) { fail("division by zero"); return; }
                result = lhs / rhs;
                break;
            case Op::Modulo:
                if (rhs == 0) { fail("division by zero"); return; }
                result = lhs % rhs;
                break;
            default: break;
        }
        values.push_back(result);
    }
};

} // namespace

extern "C" int sh_arith_eval(sh_state* st, const char* expr, long* out, const char** errmsg)
{
    return Evaluator(st, expr).run(out, errmsg);
}
