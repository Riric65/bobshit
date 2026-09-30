#!/bin/sh
# Run every ci.yml step that can run on this machine, in a throwaway clone, so
# a broken workflow is caught before it is pushed.
#
#   scripts/check-ci.sh
#
# Linux only: the macOS and mingw steps are reported as skipped.
set -u

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fail=0
ok()   { printf '  \033[32mok\033[0m    %s\n' "$1"; }
bad()  { printf '  \033[31mFAIL\033[0m  %s\n' "$1"; fail=$((fail + 1)); }
skip() { printf '  --    %s (%s)\n' "$1" "$2"; }

# a copy of the tracked files only, like a fresh clone on the runner
(cd "$root" && git ls-files | tar -cf - -T -) | (cd "$work" && tar -xf -)
if [ ! -f "$work/lang/include/common.h" ]; then
    echo "  la copie de l'arbre a échoué" >&2
    exit 1
fi

cd "$work" || exit 1
STRICT="-std=c11 -O2 -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual"

echo "job build (linux/gcc, flags stricts du workflow)"
if make -C lang clean >/dev/null 2>&1 && \
   make -C lang CC=gcc CFLAGS="$STRICT" >/dev/null 2>&1 && \
   [ "$(./lang/bobshit --version)" = "bobshit 0.1.0" ]; then
    ok "build sans warning"
else
    bad "build"
fi

echo "job build (regression + exemples + strict)"
if ./lang/tests/run.sh >/dev/null 2>&1 && make -C lang test >/dev/null 2>&1; then
    ok "regression et exemples"
else
    bad "regression ou exemples"
fi

printf 'x = [1\n' > /tmp/broken.shit
if ./lang/bobshit -s /tmp/broken.shit >/dev/null 2>&1; then
    bad "le mode strict a accepte un programme casse"
else
    ok "le mode strict rejette un programme casse"
fi

echo "job sanitize (flags exacts du workflow)"
make -C lang clean >/dev/null 2>&1
if make -C lang \
    CFLAGS="-std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra" \
    LDFLAGS="-fsanitize=address,undefined" >/dev/null 2>&1; then
    ok "build asan+ubsan (CFLAGS et LDFLAGS)"
    if ASAN_OPTIONS=detect_leaks=0 ./lang/tests/run.sh >/dev/null 2>&1 && \
       ASAN_OPTIONS=detect_leaks=0 make -C lang test >/dev/null 2>&1; then
        ok "suite sous sanitizers"
    else
        bad "suite sous sanitizers"
    fi
    if ASAN_OPTIONS=detect_leaks=0 python3 lang/tests/fuzz.py 1 40 --timeout 60 >/dev/null 2>&1; then
        ok "fuzz sous sanitizers"
    else
        bad "fuzz sous sanitizers"
    fi
else
    bad "build asan+ubsan (le LDFLAGS manque peut-etre)"
fi

echo "job fuzz (binaire normal)"
make -C lang clean >/dev/null 2>&1
make -C lang >/dev/null 2>&1
if python3 lang/tests/fuzz.py 777 120 --timeout 60 >/dev/null 2>&1; then
    ok "120 programmes corrompus"
else
    bad "fuzz"
fi

echo "job checks"
if sh -n lang/tests/run.sh 2>/dev/null; then ok "run.sh"; else bad "run.sh"; fi
if python3 -m py_compile lang/tests/fuzz.py 2>/dev/null; then ok "fuzz.py"; else bad "fuzz.py"; fi

ver=$(sed -n 's/^#define BS_VERSION "\(.*\)"$/\1/p' lang/include/common.h)
if [ -n "$ver" ] && [ "$(./lang/bobshit --version)" = "bobshit $ver" ] && \
   grep -qE "^\.TH BOBSHIT 1 .*BobShit $ver" lang/docs/bobshit.1 && \
   grep -q "$ver" lang/README.md && grep -q "$ver" README.md; then
    ok "version coherente partout ($ver)"
else
    bad "version incoherente (common.h=$ver, binaire=$(./lang/bobshit --version))"
fi

tracked=$(cd "$root" && git ls-files | grep -E '\.(o|d|exe)$|(^|/)dist/|__pycache__|(^|/)bobshit$' || true)
if [ -z "$tracked" ]; then
    ok "aucun artefact de build suivi"
else
    bad "artefacts suivis: $tracked"
fi

echo "jobs non reproductibles ici"
skip "macos x64 / macos arm64" "runners macOS"
skip "windows / mingw-w64" "il faut mingw-w64"

echo
if [ "$fail" -eq 0 ]; then
    printf 'ci simulée: tout est vert\n'
    exit 0
fi
printf 'ci simulée: %d échec(s)\n' "$fail"
exit 1
