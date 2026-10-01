#!/bin/sh
# Regression suite: run every tests/*.shit and diff against tests/*.expected
#   ./run.sh          run the suite
#   ./run.sh -v       also print the diff
#   ./run.sh --update refresh the .expected files
set -u
cd "$(dirname "$0")/.."

BS=./bobshit
[ -x "$BS" ] || make -s

update=0
verbose=0
for arg in "$@"; do
    case "$arg" in
        --update) update=1 ;;
        -v) verbose=1 ;;
    esac
done

pass=0
fail=0
failed_list=""

for src in tests/*.shit; do
    name=$(basename "$src" .shit)
    exp="tests/$name.expected"
    out=$(mktemp)
    err=$(mktemp)
    cmp=$(mktemp)

    if [ "$name" = "strict_abort" ]; then
        $BS -s "$src" >"$out" 2>"$err"
    else
        $BS "$src" >"$out" 2>"$err"
    fi
    code=$?

    got=$(cat "$out" "$err")

    if [ "$update" = 1 ]; then
        printf '%s\n' "$got" >"$exp"
        echo "updated $exp"
        pass=$((pass + 1))
        rm -f "$out" "$err"
        continue
    fi

    if [ ! -f "$exp" ]; then
        echo "MISSING EXPECTED: $exp"
        fail=$((fail + 1))
        rm -f "$out" "$err"
        continue
    fi

    if [ "$got" = "$(cat "$exp")" ]; then
        pass=$((pass + 1))
        [ "$verbose" = 1 ] && echo "ok   $name"
    else
        fail=$((fail + 1))
        failed_list="$failed_list $name"
        echo "FAIL $name"
        if [ "$verbose" = 1 ]; then
            # Only the differing lines, not both files: a full dump of two
            # 200 line files hides the two lines that matter. And the format
            # goes through an argument, never as the format itself, so a
            # leading dash cannot be read as an option by the printf builtin.
            printf '%s\n' "$got" > "$cmp"
            echo "--- $name: what differs ---"
            diff "$exp" "$cmp" | head -14 || true
        fi
    fi
    rm -f "$out" "$err" "$cmp"
done

echo
echo "passed: $pass   failed: $fail"
[ "$fail" = 0 ] || { echo "failing:$failed_list"; exit 1; }
exit 0
