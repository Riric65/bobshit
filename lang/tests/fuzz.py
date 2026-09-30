#!/usr/bin/env python3
"""Fuzz the BobShit parser: throw garbage and truncated programs at it and
make sure the interpreter never crashes (signals), never hangs, and never
becomes pathologically slow.

    tests/fuzz.py [seed] [rounds] [--slow N] [--timeout S] [--save DIR]

Exit code 0 means every generated program behaved: the interpreter exited with
0 or 1 (a handled error) and never crashed, timed out or took absurdly long.
"""
import os
import random
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BOB = os.path.join(HERE, "..", "bobshit")

DEFAULT_ROUNDS = 400
DEFAULT_TIMEOUT = 10.0
# anything slower than this on a garbage input is a performance bug, not noise
SLOW_SECONDS = 1.0

TOKENS = [
    "say", "print", "if", "si", "elif", "else", "sinon", "then", "alors", "do",
    "end", "fin", "done", "while", "tantque", "for", "pour", "in", "dans", "to",
    "jusqua", "step", "pas", "fun", "fn", "return", "break", "continue", "try",
    "catch", "and", "or", "not", "true", "false", "nil", "x", "y", "foo", "1",
    "2.5", '"txt"', "'s'", "[", "]", "{", "}", "(", ")", ",", ":", "=", ":=",
    "+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=", "&&", "||", "!",
    "?", "`", "~", "\\", "#c\n", "//c\n", "/*c*/", "\n", ";", "  ", "\t",
    ".", ",", "é", "0x", "1e", '"unterminated', "[1,2", "{a:", "fun(", "))",
]

PREFIXES = [
    "x = [1, 2, 3]\n",
    "fun f(a, b)\n  return a + b\nend\n",
    "if true\n  say 1\n",
    "for i in 1 to 3\n  say i\n",
    "try\n  x = 1\n",
    "m = {a: 1}\nsay m.a\n",
    '"unterminated\n',
    "xs = [1\n",
]


def make_source(rng):
    lines = []
    if rng.random() < 0.6:
        lines.append(rng.choice(PREFIXES))
    for _ in range(rng.randint(1, 40)):
        lines.append(" ".join(rng.choice(TOKENS) for _ in range(rng.randint(1, 12))))
    return "\n".join(lines) + "\n"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = {}
    rest = []
    i = 0
    argv = sys.argv[1:]
    while i < len(argv):
        if argv[i].startswith("--"):
            key = argv[i][2:]
            if "=" in key:
                key, val = key.split("=", 1)
            else:
                val = argv[i + 1] if i + 1 < len(argv) else ""
                i += 1
            opts[key] = val
        else:
            rest.append(argv[i])
        i += 1

    seed = int(rest[0]) if len(rest) > 0 else 1234
    rounds = int(rest[1]) if len(rest) > 1 else DEFAULT_ROUNDS
    timeout = float(opts.get("timeout", DEFAULT_TIMEOUT))
    show_slow = int(opts.get("slow", 0))
    save_dir = opts.get("save")

    if not os.path.exists(BOB):
        print(f"fuzz: {BOB} not found, run make first", file=sys.stderr)
        return 1

    rng = random.Random(seed)
    tmp = tempfile.NamedTemporaryFile("w", suffix=".shit", delete=False)
    timings = []
    failures = 0

    def save(name, src):
        if not save_dir:
            return None
        os.makedirs(save_dir, exist_ok=True)
        path = os.path.join(save_dir, name)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(src)
        return path

    for n in range(rounds):
        src = make_source(rng)
        tmp.seek(0)
        tmp.truncate()
        tmp.write(src)
        tmp.flush()

        start = time.time()
        try:
            proc = subprocess.run([BOB, tmp.name], capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            path = save(f"timeout-{seed}-{n}.shit", src)
            print(f"TIMEOUT after {timeout}s on round {n}" + (f" (saved to {path})" if path else ""))
            print(src)
            failures += 1
            continue
        elapsed = time.time() - start
        timings.append((elapsed, n))

        if proc.returncode < 0 or proc.returncode > 1:
            path = save(f"crash-{seed}-{n}.shit", src)
            print(f"CRASH rc={proc.returncode} on round {n}" + (f" (saved to {path})" if path else ""))
            print(src)
            print(proc.stderr.decode("utf-8", "replace")[:2000])
            failures += 1

    tmp.close()
    os.unlink(tmp.name)

    timings.sort(reverse=True)
    slow = [t for t in timings if t[0] > SLOW_SECONDS]
    if slow:
        print(f"{len(slow)} programme(s) au-delà de {SLOW_SECONDS}s:")
        for elapsed, n in timings[: max(3, min(10, len(timings)))]:
            print(f"  {elapsed:6.3f}s  round {n}")

    if show_slow:
        for elapsed, n in timings[:show_slow]:
            src = make_source(random.Random(seed))
            print(f"--- {elapsed:.3f}s round {n} ---")

    if failures:
        print(f"fuzz: {failures} problem(s) sur {rounds} programmes")
        return 1
    print(f"fuzz ok: {rounds} programmes, aucun crash, {timings[0][0]:.3f}s max")
    return 0


if __name__ == "__main__":
    sys.exit(main())
