# Shell interpreter self-test.
#
# Copy it to the device (e.g. the SD card) and run it from the Terminal:
#   sh /sdcard/shell_test.sh
# It prints a FAIL line per failed check and a summary, and exits with 1 when anything failed.
# Temp files are written next to the script.
#
# Command substitution nests only 3 deep here: each level keeps a temp file open, and ESP32
# filesystems allow 4 open files at a time.

pass=0
fail=0

# check <description> <expected> <actual>
check() {
    if [ "$2" = "$3" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL: $1 (expected '$2', got '$3')"
    fi
}

case $0 in
    */*) dir=${0%/*} ;;
    *) dir=. ;;
esac

echo "Shell test: writing temp files to $dir"

# --- Variables and quoting ---

x='a  b'
check "unquoted splitting" "a b" "$(echo $x)"
check "double quotes keep spaces" "a  b" "$(echo "$x")"
check "single quotes are literal" '$x' "$(echo '$x')"
check "backslash escape" '$x' "$(echo \$x)"
false
check "exit status" 1 "$?"

set -- one "two words" three
check "positional count" 3 "$#"
check "positional parameter" "two words" "$2"
n=0
for p in "$@"; do n=$((n + 1)); done
check "quoted \$@" 3 "$n"

IFS=:
set -- $(echo a:b:c)
IFS=' '
check "custom IFS" 3 "$#"

# --- Parameter expansion ---

path=a/b/c.tar.gz
check "default" def "${unset_var:-def}"
check "assign default" set "${assigned:=set}"
check "assigned variable" set "$assigned"
check "alternate" alt "${path:+alt}"
check "length" 12 "${#path}"
check "shortest prefix" b/c.tar.gz "${path#*/}"
check "longest prefix" c.tar.gz "${path##*/}"
check "shortest suffix" a/b/c.tar "${path%.*}"
check "longest suffix" a/b/c "${path%%.*}"
check "nested default" deep "${unset1:-${unset2:-deep}}"
lazy=set
check "lazy default word" set "${lazy:-$(ran=yes; echo no)}"
check "lazy word not run" "" "$ran"

# --- Arithmetic ---

a=5
expr='a+1'
check "precedence" 7 "$((1 + 2 * 3))"
check "parentheses" 9 "$(( (1 + 2) * 3 ))"
check "division and modulo" 3/1 "$((10 / 3))/$((10 % 3))"
check "unary minus" -5 "$((-3 + -2))"
check "variables" 10 "$((a * 2))"
check "compound assignment" 6 "$((a += 1))"
check "expression-valued variable" 14 "$((expr * 2))"
check "chained assignment" 4 "$((p = q = 4))"
check "hex and octal" 16/8 "$((0x10))/$((010))"
check "comparison and logic" 1 "$((1 < 2 && 3 != 4))"

# --- Control flow ---

r=
for v in 1 2 3; do
    if [ $v = 1 ]; then r="${r}one"; elif [ $v = 2 ]; then r="${r}two"; else r="${r}other"; fi
done
check "if/elif/else" onetwoother "$r"

i=0
while [ $i -lt 1000 ]; do i=$((i + 1)); done
check "1000-iteration while loop" 1000 "$i"

j=0
until [ $j -ge 3 ]; do j=$((j + 1)); done
check "until loop" 3 "$j"

s=
for i in 1 2 3 4; do
    if [ $i = 2 ]; then continue; fi
    if [ $i = 4 ]; then break; fi
    s="$s$i"
done
check "break and continue" 13 "$s"

t=
for a in 1 2; do for b in 1 2; do if [ $b = 2 ]; then break 2; fi; t="$t$a$b"; done; done
check "break 2" 11 "$t"

r=
for f in a.txt b.c x; do
    case $f in
        *.txt) r="${r}T" ;;
        a|b.c) r="${r}C" ;;
        [xy]) r="${r}X" ;;
        *) r="${r}?" ;;
    esac
done
check "case patterns" TCX "$r"

false && x1=1 || x2=2
check "and-or lists" "/2" "$x1/$x2"

# --- Functions ---

greet() {
    g="hello $1"
    return 4
}
greet world
check "function return status" 4 "$?"
check "function arguments" "hello world" "$g"

outer=outer
scoped() {
    local outer=inner
    seen=$outer
}
scoped
check "local inside function" inner "$seen"
check "local restored" outer "$outer"

fact() {
    if [ $1 -le 1 ]; then f=1; else fact $(($1 - 1)); f=$((f * $1)); fi
}
fact 6
check "recursive factorial" 720 "$f"

# --- Subshells, command substitution, eval ---

y=1
(y=2)
check "subshell isolation" 1 "$y"
(exit 3)
check "subshell exit status" 3 "$?"
{ y=4; }
check "brace group" 4 "$y"

check "nested command substitution" deep "$(echo $(echo $(echo deep)))"
check "trailing newlines stripped" a "$(echo a; echo; echo)"
check "backticks" tick "`echo tick`"
eval 'e=5'
check "eval" 5 "$e"

# --- Redirection, here-documents, pipelines ---

tmp="$dir/shell_test.tmp"
echo one > "$tmp"
echo two >> "$tmp"
{ read l1; read l2; } < "$tmp"
check "redirect and append" one/two "$l1/$l2"
check "stderr to stdout" "out err" "$( { echo out; echo err >&2; } 2>&1 | { read o; read e; echo $o $e; } )"

hd=w
read h1 <<EOF
hello $hd
EOF
read h2 <<'EOF'
hello $hd
EOF
check "here-document" "hello w" "$h1"
check "quoted here-document" 'hello $hd' "$h2"

lines=
while read l; do lines="$lines[$l]"; done <<EOF
a
b
EOF
check "while-read from here-document" "[a][b]" "$lines"
check "pipeline of builtins" "b c" "$(echo a b c | { read p q; echo $q; })"

echo 'sourced=9; first=$1' > "$tmp"
. "$tmp" arg
check "source with arguments" 9/arg "$sourced/$first"

# --- test builtin ---

[ ! -z x ] && [ \( a = a \) -a \( b = c -o d = d \) ]
check "test grouping and negation" 0 "$?"
test -f "$tmp"
check "test -f" 0 "$?"
test -d "$tmp"
check "test -d" 1 "$?"

# --- Deep nesting (the interpreter must not run out of stack) ---

depth() {
    if [ $1 -lt 50 ]; then depth $(($1 + 1)); else deepest=$1; fi
}
depth 0
check "50 nested function calls" 50 "$deepest"

open=; close=; i=0
while [ $i -lt 100 ]; do open="$open{ "; close="$close} "; i=$((i + 1)); done
eval "${open}r=groups; ${close}"
check "100 nested brace groups" groups "$r"

open=; close=; i=0
while [ $i -lt 100 ]; do open="$open\${u:-"; close="$close}"; i=$((i + 1)); done
eval "r=${open}braces${close}"
check "100 nested \${...}" braces "$r"

open=; close=; i=0
while [ $i -lt 100 ]; do open="$open("; close="$close)"; i=$((i + 1)); done
eval "r=\$(( ${open}1 + 1${close} ))"
check "100 nested arithmetic parentheses" 2 "$r"

# --- Scripts run as their own sh instance ---

echo 'exit 7' > "$tmp"
sh "$tmp"
check "sh script exit status" 7 "$?"

open=; close=; i=0
while [ $i -lt 300 ]; do open="$open{ "; close="$close} "; i=$((i + 1)); done
echo "${open}x=1; ${close}" > "$tmp"
sh "$tmp"
check "excessive nesting is reported, not a crash" 2 "$?"

echo 'x=$((1 / 0)); echo unreachable' > "$tmp"
check "arithmetic error aborts the script" "" "$(sh "$tmp")"

echo 'set -e; false; echo unreachable' > "$tmp"
check "set -e" "" "$(sh "$tmp")"

rm "$tmp"

# --- Summary ---

echo "passed: $pass, failed: $fail"
if [ $fail -gt 0 ]; then
    exit 1
fi
