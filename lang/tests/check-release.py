#!/usr/bin/env python3
"""Replay the release workflow locally, in a throwaway copy of the tree.

The release assembles tarballs, a .deb, an .rpm and a Windows zip inline in
the workflow, where a mistake only surfaces minutes into a release. This runs
the steps a Linux box can reproduce and reports the others as skipped.

    tests/check-release.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WF = os.path.join(ROOT, ".github", "workflows", "release.yml")

GREEN, RED, YEL, OFF = "\033[32m", "\033[31m", "\033[33m", "\033[0m"

# GitHub expressions -> shell variables we control here
EXPRS = {
    "matrix.cc": "CCSEL",
    "matrix.cross": "CROSS",
    "matrix.arch": "ARCH",
    "matrix.static": "MATSTATIC",
    "matrix.target": "TARGET",
    "matrix.optional": "OPTIONAL",
    "needs.verify.outputs.version": "VER",
    "github.ref_name": "TAGNAME",
    "github.token": "TOKEN",
}


def substitute(script, values):
    def repl(m):
        key = m.group(1).strip()
        if key in EXPRS:
            return '"$%s"' % EXPRS[key]
        return "__GH_EXPR__"
    return re.sub(r"\$\{\{(.+?)\}\}", repl, script)


def steps():
    data = yaml.safe_load(open(WF, encoding="utf-8"))
    for job_name, job in data["jobs"].items():
        for step in job.get("steps") or []:
            if step.get("run"):
                yield job_name, step.get("name", "?"), step["run"]


# steps that only install toolchains: they need root, so they cannot be
# replayed locally and are reported as skipped
APT_STEPS = ("install the toolchain", "install mingw-w64", "install dpkg-dev", "install rpm")


def main():
    ver = None
    for line in open(os.path.join(ROOT, "lang/include/common.h"), encoding="utf-8"):
        if line.startswith("#define BS_VERSION"):
            ver = line.split('"')[1]
            break

    work = tempfile.mkdtemp()
    dst = os.path.join(work, "bobshit", "src")
    shutil.copytree(ROOT, dst, ignore=shutil.ignore_patterns(
        ".git", "dist", "__pycache__", "*.o", "*.d", "bobshit"))

    env_base = {
        # step level env vars used by the linux job
        "CC": "cc",
        "ARCH": "x86_64",
        "MATSTATIC": "",
        "RUNNER_TEMP": work,
        "GITHUB_REF_NAME": "v%s" % ver,
        "GITHUB_REF_TYPE": "tag",
        "GITHUB_OUTPUT": os.path.join(work, "gh_output"),
        "GITHUB_STEP_SUMMARY": os.path.join(work, "gh_summary"),
        "VER": ver,
        "TAGNAME": "v%s" % ver,
        "TOKEN": "",
        "CCSEL": "cc",
        "CROSS": "",
        "ARCH": "x86_64",
        "MATSTATIC": "",
        "TARGET": "arm64",
        "OPTIONAL": "false",
    }
    # the workflow level env: (CFLAGS_REL: ...)
    for k, v in (yaml.safe_load(open(WF, encoding="utf-8")).get("env") or {}).items():
        env_base.setdefault(k, v)
    open(env_base["GITHUB_OUTPUT"], "w").close()

    fails = 0

    def report(label, ok, detail=""):
        nonlocal fails
        if ok:
            print("  %sok%s    %s" % (GREEN, OFF, label))
        else:
            fails += 1
            print("  %sFAIL%s  %s" % (RED, OFF, label))
            if detail:
                for line in detail.strip().splitlines()[-6:]:
                    print("          " + line)

    def run(script, extra=None, cwd=None):
        env = dict(os.environ)
        env.update(env_base)
        env.update(extra or {})
        p = subprocess.run(["bash", "-euxo", "pipefail", "-c", substitute(script, env)],
                           cwd=cwd or dst, env=env, capture_output=True, text=True)
        return p

    def listing(sub=""):
        d = os.path.join(dst, "dist", sub) if sub else os.path.join(dst, "dist")
        if not os.path.isdir(d):
            return []
        return sorted(f for f in os.listdir(d) if os.path.isfile(os.path.join(d, f)))

    print("version: %s\n" % ver)

    for job, label, script in steps():
        tag = "%s / %s" % (job, label)

        if label in APT_STEPS:
            print("  %sskip%s  %s (installation de toolchain: needs root)" % (YEL, OFF, tag))

        elif job == "verify":
            report(tag, run(script).returncode == 0, run.__doc__ or "")

        elif job == "source":
            p = run(script)
            report(tag, p.returncode == 0 and bool(listing()), p.stderr[-400:])

        elif job == "linux" and "toolchain" not in label:
            p = run(script)
            report(tag, p.returncode == 0 and bool(listing()), p.stderr[-400:])

        elif job == "linux":
            print("  %sskip%s  %s (aarch64 et musl: autre toolchain)" % (YEL, OFF, tag))

        elif job == "windows" and "cross compile" in label:
            if shutil.which("x86_64-w64-mingw32-gcc"):
                report(tag, run(script).returncode == 0)
            else:
                print("  %sskip%s  %s (mingw-w64 absent)" % (YEL, OFF, tag))

        elif job == "windows":
            # the zip assembly needs the exe produced by the previous step
            open(os.path.join(dst, "bobshit.exe"), "wb").write(b"MZ" + b"\0" * 64)
            p = run(script)
            report(tag, p.returncode == 0 and any(f.endswith(".zip") for f in listing()),
                   p.stderr[-400:])

        elif job == "macos":
            print("  %sskip%s  %s (runner macOS)" % (YEL, OFF, tag))

        elif job == "macos-intel":
            print("  %sskip%s  %s (runner macOS Intel, hors du chemin critique)" % (YEL, OFF, tag))

        elif job in ("deb", "rpm"):
            tool = "dpkg-deb" if job == "deb" else "rpmbuild"
            ext = ".deb" if job == "deb" else ".rpm"
            if not shutil.which(tool):
                print("  %sskip%s  %s (%s absent)" % (YEL, OFF, tag, tool))
            else:
                p = run(script)
                got = [f for f in listing() if f.endswith(ext)]
                report(tag, p.returncode == 0 and bool(got), p.stderr[-500:])

        elif job == "release":
            print("  %sskip%s  %s (publie sur GitHub)" % (YEL, OFF, tag))

    print("\nartefacts produits localement:")
    for f in listing():
        print("  %9d %s" % (os.path.getsize(os.path.join(dst, "dist", f)), f))
    if listing():
        print("\ncontenu du zip windows:")
        import zipfile
        for z in [f for f in listing() if f.endswith(".zip")]:
            with zipfile.ZipFile(os.path.join(dst, "dist", z)) as zf:
                for n in sorted(zf.namelist()):
                    print("   ", n)

    shutil.rmtree(work, ignore_errors=True)
    print()
    print("release simulée: tout est vert" if fails == 0
          else "release simulée: %d échec(s)" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
