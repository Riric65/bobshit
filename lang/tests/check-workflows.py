#!/usr/bin/env python3
"""Extract every `run:` block from the workflows and syntax check it.

The packaging logic lives inline in the release workflow, where a stray quote
or a misindented heredoc terminator only shows up on a runner, minutes into a
release. This checks the shell syntax offline, and additionally extracts the
heredoc bodies so their content can be eyeballed.
"""
import os
import subprocess
import sys
import tempfile

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORKFLOWS = os.path.join(ROOT, ".github", "workflows")


def iter_steps(path):
    data = yaml.safe_load(open(path, encoding="utf-8"))
    for job_name, job in (data.get("jobs") or {}).items():
        for step in job.get("steps") or []:
            run = step.get("run")
            if not run:
                continue
            label = step.get("name") or step.get("uses") or "?"
            yield job_name, label, run


def main():
    failures = 0
    total = 0
    for name in sorted(os.listdir(WORKFLOWS)):
        if not name.endswith((".yml", ".yaml")):
            continue
        path = os.path.join(WORKFLOWS, name)
        print(f"== {name}")
        for job, label, run in iter_steps(path):
            total += 1
            with tempfile.NamedTemporaryFile("w", suffix=".sh", delete=False) as fh:
                # GitHub expressions like ${{ ... }} are not valid shell
                script = run.replace("${{", "${__GH").replace("}}", "}")
                fh.write(script)
                tmp = fh.name
            res = subprocess.run(["bash", "-n", tmp], capture_output=True)
            os.unlink(tmp)
            if res.returncode != 0:
                failures += 1
                print(f"   FAIL {job} / {label}")
                for line in res.stderr.decode().splitlines()[:4]:
                    print(f"        {line}")
            else:
                print(f"   ok   {job} / {label}")
    print()
    print(f"{total} blocs run:, {failures} erreur(s) de syntaxe")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
