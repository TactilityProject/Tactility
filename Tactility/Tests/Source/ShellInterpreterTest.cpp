#include "doctest.h"

#include <tactility/paths.h>

extern "C" {
#include <Tactility/app/shell/shell/sh.h>
}

#include <cerrno>
#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace {

void makeDirectories(const std::string& path) {
    for (size_t i = 1; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            mkdir(path.substr(0, i).c_str(), 0755);
        }
    }
}

/** A fresh interpreter state per test, with $TMP pointing at a writable scratch directory. */
class Shell {
public:
    sh_state state {};
    std::string tmp;

    Shell() {
        char path[FILE_MAX_PATH_STRING_LENGTH];
        REQUIRE_EQ(paths_get_temp_path(path, sizeof(path)), ERROR_NONE);
        tmp = std::string(path) + "/shelltest";
        makeDirectories(tmp);
        sh_state_init(&state);
        sh_set(&state, "TMP", tmp.c_str());
    }

    ~Shell() {
        sh_state_free(&state);
    }

    int run(const char* source) {
        return sh_run_string(&state, source);
    }

    std::string var(const char* name) {
        const char* value = sh_get(&state, name);
        return value != nullptr ? value : "<unset>";
    }
};

} // namespace

// region Variables and quoting

TEST_CASE("shell: assignment and command substitution") {
    Shell sh;
    CHECK_EQ(sh.run("x=1; y=$(echo hi)"), 0);
    CHECK_EQ(sh.var("x"), "1");
    CHECK_EQ(sh.var("y"), "hi");
}

TEST_CASE("shell: exit status in $?") {
    Shell sh;
    sh.run("false; a=$?; true; b=$?");
    CHECK_EQ(sh.var("a"), "1");
    CHECK_EQ(sh.var("b"), "0");
}

TEST_CASE("shell: quoting and word splitting") {
    Shell sh;
    sh.run("x='a  b'; unquoted=$(echo $x); quoted=$(echo \"$x\"); single='$x'; escaped=\\$x");
    CHECK_EQ(sh.var("unquoted"), "a b");
    CHECK_EQ(sh.var("quoted"), "a  b");
    CHECK_EQ(sh.var("single"), "$x");
    CHECK_EQ(sh.var("escaped"), "$x");
}

TEST_CASE("shell: quoted $@ without parameters") {
    Shell sh;
    sh.run(
        "set --\n"
        "n=0; for a in \"$@\"; do n=$((n+1)); done\n"
        "p=0; for a in x\"$@\"; do p=$((p+1)); v=$a; done\n"
        "s=0; for a in \"$*\"; do s=$((s+1)); done"
    );
    CHECK_EQ(sh.var("n"), "0");
    CHECK_EQ(sh.var("p"), "1");
    CHECK_EQ(sh.var("v"), "x");
    CHECK_EQ(sh.var("s"), "1");
}

TEST_CASE("shell: IFS splitting") {
    Shell sh;
    sh.run("IFS=:; set -- $(echo a:b:c); n=$#; second=$2");
    CHECK_EQ(sh.var("n"), "3");
    CHECK_EQ(sh.var("second"), "b");
}

TEST_CASE("shell: positional parameters") {
    Shell sh;
    sh.run("set -- a 'b c' d; n=$#; second=$2; all=\"$*\"; count=0; for p in \"$@\"; do count=$((count+1)); done");
    CHECK_EQ(sh.var("n"), "3");
    CHECK_EQ(sh.var("second"), "b c");
    CHECK_EQ(sh.var("all"), "a b c d");
    CHECK_EQ(sh.var("count"), "3");
}

TEST_CASE("shell: parameter expansion operators") {
    Shell sh;
    sh.run(
        "p=a/b/c; empty=\n"
        "d1=${unset:-def}; d2=${unset-def}; d3=${empty:-def}; d4=${empty-def}\n"
        ": ${assigned:=set}\n"
        "alt=${p:+alt}; noalt=${unset:+alt}; len=${#p}\n"
        "s1=${p#*/}; s2=${p##*/}; s3=${p%/*}; s4=${p%%/*}\n"
        "nested=${unset:-${unset2:-deep}}"
    );
    CHECK_EQ(sh.var("d1"), "def");
    CHECK_EQ(sh.var("d2"), "def");
    CHECK_EQ(sh.var("d3"), "def");
    CHECK_EQ(sh.var("d4"), "");
    CHECK_EQ(sh.var("assigned"), "set");
    CHECK_EQ(sh.var("alt"), "alt");
    CHECK_EQ(sh.var("noalt"), "");
    CHECK_EQ(sh.var("len"), "5");
    CHECK_EQ(sh.var("s1"), "b/c");
    CHECK_EQ(sh.var("s2"), "c");
    CHECK_EQ(sh.var("s3"), "a/b");
    CHECK_EQ(sh.var("s4"), "a");
    CHECK_EQ(sh.var("nested"), "deep");
}

TEST_CASE("shell: default word is only expanded when needed") {
    Shell sh;
    sh.run("x=set; y=${x:-$(ran=yes; echo v)}");
    CHECK_EQ(sh.var("y"), "set");
    CHECK_EQ(sh.var("ran"), "<unset>");
}

TEST_CASE("shell: unset and set -u") {
    Shell sh;
    sh.run("x=1; unset x");
    CHECK_EQ(sh.var("x"), "<unset>");

    Shell strict;
    CHECK_EQ(strict.run("set -u; y=$undefined_variable; after=1"), 2);
    CHECK_EQ(strict.var("after"), "<unset>");
}

// endregion

// region Arithmetic

TEST_CASE("shell: arithmetic") {
    Shell sh;
    sh.run(
        "a=5; c='a+1'\n"
        "r1=$((1+2*3)); r2=$(( (1+2)*3 )); r3=$((10/3)); r4=$((10%3)); r5=$((-3 + -2))\n"
        "r6=$((a*2)); r7=$((a+=1)); r8=$((1<2)); r9=$((1==2 || 3)); r10=$((0x10)); r11=$((010))\n"
        "r12=$((!0)); r13=$((1,2)); r14=$((c*2)); r15=$(( ((((2)))) ))"
    );
    CHECK_EQ(sh.var("r1"), "7");
    CHECK_EQ(sh.var("r2"), "9");
    CHECK_EQ(sh.var("r3"), "3");
    CHECK_EQ(sh.var("r4"), "1");
    CHECK_EQ(sh.var("r5"), "-5");
    CHECK_EQ(sh.var("r6"), "10");
    CHECK_EQ(sh.var("r7"), "6");
    CHECK_EQ(sh.var("a"), "6");
    CHECK_EQ(sh.var("r8"), "1");
    CHECK_EQ(sh.var("r9"), "1");
    CHECK_EQ(sh.var("r10"), "16");
    CHECK_EQ(sh.var("r11"), "8");
    CHECK_EQ(sh.var("r12"), "1");
    CHECK_EQ(sh.var("r13"), "2");
    CHECK_EQ(sh.var("r14"), "14");
    CHECK_EQ(sh.var("r15"), "2");
}

TEST_CASE("shell: arithmetic variables, assignment and precedence") {
    Shell sh;
    sh.run(
        "b='2*3'; m='1+1'\n"
        "r1=$((b+1)); r2=$((p=q=4)); r3=$((m*=3)); r4=$((- -3)); r5=$((!!5))\n"
        "r6=$((1+2*3-4/2)); r7=$((2<3==1)); r8=$(( ((((((1+1)))))) )); r9=$((-b))"
    );
    CHECK_EQ(sh.var("r1"), "7");
    CHECK_EQ(sh.var("r2"), "4");
    CHECK_EQ(sh.var("p"), "4");
    CHECK_EQ(sh.var("q"), "4");
    CHECK_EQ(sh.var("r3"), "6");
    CHECK_EQ(sh.var("m"), "6");
    CHECK_EQ(sh.var("r4"), "3");
    CHECK_EQ(sh.var("r5"), "1");
    CHECK_EQ(sh.var("r6"), "5");
    CHECK_EQ(sh.var("r7"), "1");
    CHECK_EQ(sh.var("r8"), "2");
    CHECK_EQ(sh.var("r9"), "-6");
}

TEST_CASE("shell: arithmetic syntax errors") {
    const std::string scripts[] = {
        "x=$((1+)); after=1", "x=$((1 2)); after=1", "v='(1'; x=$((v)); after=1", "x=$((1)+1)); after=1",
        "x=$((2**3)); after=1", "v='1)'; x=$((v)); after=1", "r=r; x=$((r)); after=1"
    };
    for (const std::string& script : scripts) {
        Shell sh;
        CAPTURE(script);
        CHECK_EQ(sh.run(script.c_str()), 2);
        CHECK_EQ(sh.var("after"), "<unset>");
    }
}

TEST_CASE("shell: arithmetic errors are fatal") {
    Shell sh;
    CHECK_EQ(sh.run("x=$((1/0)); after=1"), 2);
    CHECK_EQ(sh.var("after"), "<unset>");
}

// endregion

// region Control flow

TEST_CASE("shell: if, elif and else") {
    Shell sh;
    sh.run(
        "for v in 1 2 3; do\n"
        "  if [ $v = 1 ]; then r=\"${r}one\"; elif [ $v = 2 ]; then r=\"${r}two\"; else r=\"${r}other\"; fi\n"
        "done"
    );
    CHECK_EQ(sh.var("r"), "onetwoother");
}

TEST_CASE("shell: while and until loops") {
    Shell sh;
    sh.run("i=0; while [ $i -lt 5 ]; do i=$((i+1)); done; j=0; until [ $j -ge 3 ]; do j=$((j+1)); done");
    CHECK_EQ(sh.var("i"), "5");
    CHECK_EQ(sh.var("j"), "3");
}

TEST_CASE("shell: break and continue") {
    Shell sh;
    sh.run(
        "for i in 1 2 3 4; do if [ $i = 2 ]; then continue; fi; if [ $i = 4 ]; then break; fi; s=\"$s$i\"; done\n"
        "for a in 1 2; do for b in 1 2; do if [ $b = 2 ]; then break 2; fi; t=\"$t$a$b\"; done; done"
    );
    CHECK_EQ(sh.var("s"), "13");
    CHECK_EQ(sh.var("t"), "11");
}

TEST_CASE("shell: case patterns") {
    Shell sh;
    sh.run(
        "for f in a.txt b.c x; do\n"
        "  case $f in *.txt) r=\"${r}T\";; a|b.c) r=\"${r}C\";; [xy]) r=\"${r}X\";; *) r=\"${r}?\";; esac\n"
        "done"
    );
    CHECK_EQ(sh.var("r"), "TCX");
}

TEST_CASE("shell: and-or lists and negation") {
    Shell sh;
    sh.run("false && x=1 || y=2; ! false; neg=$?");
    CHECK_EQ(sh.var("x"), "<unset>");
    CHECK_EQ(sh.var("y"), "2");
    CHECK_EQ(sh.var("neg"), "0");
}

TEST_CASE("shell: exit") {
    Shell sh;
    CHECK_EQ(sh.run("exit 3; after=1"), 3);
    CHECK_EQ(sh.var("after"), "<unset>");
}

// endregion

// region Functions

TEST_CASE("shell: functions, arguments, return and local") {
    Shell sh;
    sh.run(
        "greet() { g=\"hello $1\"; return 4; }\n"
        "greet world; status=$?\n"
        "x=outer; scoped() { local x=inner; seen=$x; }; scoped\n"
        "fact() { if [ $1 -le 1 ]; then r=1; else fact $(($1-1)); r=$((r*$1)); fi; }; fact 5"
    );
    CHECK_EQ(sh.var("g"), "hello world");
    CHECK_EQ(sh.var("status"), "4");
    CHECK_EQ(sh.var("seen"), "inner");
    CHECK_EQ(sh.var("x"), "outer");
    CHECK_EQ(sh.var("r"), "120");
}

TEST_CASE("shell: return from inside a loop") {
    Shell sh;
    sh.run("f() { for i in 1 2 3; do if [ $i = 2 ]; then return 7; fi; last=$i; done; }; f; s=$?");
    CHECK_EQ(sh.var("s"), "7");
    CHECK_EQ(sh.var("last"), "1");
}

TEST_CASE("shell: function recursion limit") {
    Shell sh;
    sh.run("f() { f; }; f; s=$?");
    CHECK_EQ(sh.var("s"), "1");
}

// endregion

// region Subshells, groups and command substitution

TEST_CASE("shell: subshell isolation and groups") {
    Shell sh;
    sh.run("x=1; (x=2); a=$x; (exit 3); s=$?; { x=4; }; b=$x");
    CHECK_EQ(sh.var("a"), "1");
    CHECK_EQ(sh.var("s"), "3");
    CHECK_EQ(sh.var("b"), "4");
}

TEST_CASE("shell: nested command substitution") {
    Shell sh;
    sh.run("x=$(echo $(echo $(echo deep))); y=$(echo a; echo; echo); z=`echo tick`");
    CHECK_EQ(sh.var("x"), "deep");
    CHECK_EQ(sh.var("y"), "a");
    CHECK_EQ(sh.var("z"), "tick");
}

TEST_CASE("shell: command substitution drops NUL bytes") {
    Shell sh;
    sh.run("x=$(echo 'a\\0b')");
    CHECK_EQ(sh.var("x"), "ab");
}

TEST_CASE("shell: eval") {
    Shell sh;
    sh.run("eval 'x=5'; eval \"y=\\$x\"");
    CHECK_EQ(sh.var("x"), "5");
    CHECK_EQ(sh.var("y"), "5");
}

TEST_CASE("shell: source") {
    Shell sh;
    sh.run(
        "echo 'z=9; a1=$1' > $TMP/sourced.sh\n"
        ". $TMP/sourced.sh first\n"
        "echo 'return 5; never=1' > $TMP/returning.sh\n"
        "source $TMP/returning.sh; s=$?"
    );
    CHECK_EQ(sh.var("z"), "9");
    CHECK_EQ(sh.var("a1"), "first");
    CHECK_EQ(sh.var("s"), "5");
    CHECK_EQ(sh.var("never"), "<unset>");
}

// endregion

// region Redirection and pipelines

TEST_CASE("shell: output and input redirection") {
    Shell sh;
    sh.run("echo one > $TMP/f; echo two >> $TMP/f; { read a; read b; } < $TMP/f");
    CHECK_EQ(sh.var("a"), "one");
    CHECK_EQ(sh.var("b"), "two");
}

TEST_CASE("shell: stderr duplication") {
    Shell sh;
    sh.run("x=$( { echo out; echo err >&2; } 2>&1 )");
    CHECK_EQ(sh.var("x"), "out\nerr");
}

TEST_CASE("shell: here-documents") {
    Shell sh;
    sh.run("x=w\nread a <<EOF\nhello $x\nEOF\nread b <<'EOF'\nhello $x\nEOF\n");
    CHECK_EQ(sh.var("a"), "hello w");
    CHECK_EQ(sh.var("b"), "hello $x");
}

TEST_CASE("shell: while-read loop fed by a here-document") {
    Shell sh;
    sh.run("while read l; do s=\"$s[$l]\"; done <<EOF\na\nb\nEOF\n");
    CHECK_EQ(sh.var("s"), "[a][b]");
}

TEST_CASE("shell: pipelines") {
    Shell sh;
    sh.run("x=$(echo a b c | { read p q; echo $q; }); echo hello | read v; r=$v");
    CHECK_EQ(sh.var("x"), "b c");
    CHECK_EQ(sh.var("r"), "hello");
}

// endregion

// region Builtins and globbing

TEST_CASE("shell: test builtin") {
    Shell sh;
    sh.run(
        "[ 1 -lt 2 ]; a=$?; [ ! -z x ]; b=$?; [ \\( a = a \\) -a b = c ]; c=$?\n"
        "echo x > $TMP/exists; test -f $TMP/exists; d=$?; test -d $TMP/exists; e=$?"
    );
    CHECK_EQ(sh.var("a"), "0");
    CHECK_EQ(sh.var("b"), "0");
    CHECK_EQ(sh.var("c"), "1");
    CHECK_EQ(sh.var("d"), "0");
    CHECK_EQ(sh.var("e"), "1");
}

TEST_CASE("shell: test builtin negation and grouping") {
    Shell sh;
    sh.run(
        "[ ! ! x ]; a=$?; [ ! \\( a = b \\) ]; b=$?; [ \\( \\( x \\) \\) -o '' ]; c=$?\n"
        "[ a = a -a \\( b = c -o d = d \\) ]; d=$?; [ \\( a ]; e=$?; [ a -a ]; f=$?"
    );
    CHECK_EQ(sh.var("a"), "0");
    CHECK_EQ(sh.var("b"), "0");
    CHECK_EQ(sh.var("c"), "0");
    CHECK_EQ(sh.var("d"), "0");
    CHECK_EQ(sh.var("e"), "2");
    CHECK_EQ(sh.var("f"), "1");
}

TEST_CASE("shell: pattern matching with multiple stars") {
    Shell sh;
    sh.run(
        "p=a/b/c.tar.gz\n"
        "case abcabc in *b*c*c) m1=y;; esac; case abc in *x*) m2=y;; *) m2=n;; esac\n"
        "case ab in a*b*) m3=y;; esac; case a\\*b in 'a*b') m4=y;; esac\n"
        "s1=${p%.*}; s2=${p%%.*}; s3=${p#*.}; s4=${p##*/*.}"
    );
    CHECK_EQ(sh.var("m1"), "y");
    CHECK_EQ(sh.var("m2"), "n");
    CHECK_EQ(sh.var("m3"), "y");
    CHECK_EQ(sh.var("s1"), "a/b/c.tar");
    CHECK_EQ(sh.var("s2"), "a/b/c");
    CHECK_EQ(sh.var("s3"), "tar.gz");
    CHECK_EQ(sh.var("s4"), "gz");
}

TEST_CASE("shell: echo options and escapes") {
    Shell sh;
    sh.run("a=$(echo -n x; echo y); b=$(echo 'p\\tq')");
    CHECK_EQ(sh.var("a"), "xy");
    CHECK_EQ(sh.var("b"), "p\tq");
}

TEST_CASE("shell: pathname globbing") {
    Shell sh;
    sh.run(
        "echo > $TMP/g1.txt; echo > $TMP/g2.txt; echo > $TMP/g3.log\n"
        "n=0; for f in $TMP/g*.txt; do n=$((n+1)); done; none=$(echo $TMP/nomatch*.zzz)"
    );
    CHECK_EQ(sh.var("n"), "2");
    CHECK_EQ(sh.var("none"), sh.tmp + "/nomatch*.zzz");
}

// endregion

// region errexit

TEST_CASE("shell: set -e") {
    Shell sh;
    CHECK_EQ(sh.run("set -e; false; after=1"), 1);
    CHECK_EQ(sh.var("after"), "<unset>");

    Shell exempt;
    exempt.run("set -e; if false; then :; fi; false || true; ! true; x=1");
    CHECK_EQ(exempt.var("x"), "1");
}

// endregion

// region Syntax errors

TEST_CASE("shell: syntax errors") {
    Shell sh;
    CHECK_EQ(sh.run("if true; then"), 2);
    CHECK_EQ(sh.run("echo 'unterminated"), 2);
    CHECK_EQ(sh.run("case x in"), 2);
    CHECK_EQ(sh.run("x=1"), 0);
}

// endregion

// region Deep nesting

namespace {

std::string repeat(const std::string& text, int count) {
    std::string result;
    for (int i = 0; i < count; i++) result += text;
    return result;
}

} // namespace

TEST_CASE("shell: deep command substitution nesting") {
    Shell sh;
    sh.run(("x=" + repeat("$(echo ", 80) + "deep" + repeat(")", 80)).c_str());
    CHECK_EQ(sh.var("x"), "deep");
}

TEST_CASE("shell: excessive nesting fails cleanly and the shell keeps working") {
    Shell sh;
    // Parse nesting
    CHECK_EQ(sh.run((repeat("{ ", 300) + "x=1; " + repeat("} ", 300)).c_str()), 2);
    CHECK_EQ(sh.var("x"), "<unset>");
    // Execution nesting
    CHECK_EQ(sh.run(("for i in 1; do y=" + repeat("$(echo ", 200) + "deep" + repeat(")", 200) + "; done").c_str()), 2);
    CHECK_EQ(sh.var("y"), "<unset>");
    CHECK_EQ(sh.state.loop_depth, 0);
    CHECK_EQ(sh.state.call_depth, 0);
    CHECK_EQ(sh.run("for i in 1 2; do z=$i; done; w=$(echo ok)"), 0);
    CHECK_EQ(sh.var("z"), "2");
    CHECK_EQ(sh.var("w"), "ok");
}

#if defined(__linux__)

#include <pthread.h>

#include <cstdlib>
#include <cstring>

// platform-posix wraps pthread_attr_setstack() into a no-op, the linker still provides the real one.
extern "C" int __real_pthread_attr_setstack(pthread_attr_t* attr, void* stackaddr, size_t stacksize);

namespace {

struct StackRun {
    sh_state* state;
    const char* script;
    int status;
};

/**
 * Runs a script on a thread with a pattern-filled stack and returns how many bytes of that stack it touched.
 * Only for scripts that stay inside the interpreter: this thread is not a FreeRTOS task.
 */
size_t runMeasuringStack(sh_state* state, const std::string& script, int* status) {
    constexpr size_t STACK_SIZE = 512 * 1024;
    constexpr unsigned char PATTERN = 0xA5;
    auto* stack = static_cast<unsigned char*>(aligned_alloc(4096, STACK_SIZE));
    memset(stack, PATTERN, STACK_SIZE);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    __real_pthread_attr_setstack(&attr, stack, STACK_SIZE);
    StackRun run { state, script.c_str(), -1 };
    pthread_t thread;
    pthread_create(&thread, &attr, [](void* parameter) -> void* {
        auto* run = static_cast<StackRun*>(parameter);
        run->status = sh_run_string(run->state, run->script);
        return nullptr;
    }, &run);
    pthread_join(thread, nullptr);
    pthread_attr_destroy(&attr);

    // The stack grows down: everything above the lowest overwritten byte was used.
    size_t untouched = 0;
    while (untouched < STACK_SIZE && stack[untouched] == PATTERN) untouched++;
    free(stack);
    *status = run.status;
    return STACK_SIZE - untouched;
}

} // namespace

TEST_CASE("shell: native stack use does not grow with nesting") {
    Shell sh;
    int status = -1;
    size_t baseline = runMeasuringStack(&sh.state, "f() { r=$1; }; if [ 1 = 1 ]; then f ${u:-v}; fi", &status);
    CHECK_EQ(status, 0);

    const std::string scripts[] = {
        // 60 nested function calls
        "f() { if [ $1 -lt 60 ]; then f $(($1+1)); else r=$1; fi; }; f 0",
        // 150 nested brace groups
        repeat("{ ", 150) + "r=groups; " + repeat("} ", 150),
        // 200 nested ${...:-...} defaults
        "r=" + repeat("${u:-", 200) + "braces" + repeat("}", 200),
        // 60 nested loops, each iterating once
        repeat("for i in 1; do ", 60) + "r=loops; " + repeat("done; ", 60),
        // 200 nested arithmetic parentheses
        "r=$((" + repeat("(", 200) + "1" + repeat(")", 200) + "))",
    };
    const char* expected[] = { "60", "groups", "braces", "loops", "1" };

    for (size_t i = 0; i < std::size(scripts); i++) {
        CAPTURE(i);
        Shell deep;
        size_t used = runMeasuringStack(&deep.state, scripts[i], &status);
        CHECK_EQ(status, 0);
        CHECK_EQ(deep.var("r"), expected[i]);
        CHECK_LE(used, baseline + 2048);
    }
}

#endif

// endregion
