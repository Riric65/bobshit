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


class StrictLoader(yaml.SafeLoader):
    """GitHub rejects a workflow with a duplicate key, so we do too."""


def _no_duplicates(loader, node, deep=False):
    mapping = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in mapping:
            raise yaml.constructor.ConstructorError(
                None, None, f"duplicate key {key!r} (GitHub would reject the file)",
                key_node.start_mark)
        mapping[key] = loader.construct_object(value_node, deep=deep)
    return mapping


StrictLoader.add_constructor(
    yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _no_duplicates)


def iter_steps(path):
    data = yaml.load(open(path, encoding="utf-8"), Loader=StrictLoader)
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
        # an invalid workflow is silently replaced by a GitHub stub, so the
        # YAML has to be checked before anything else
        try:
            yaml.load(open(path, encoding="utf-8").read(), Loader=StrictLoader)
            print("   ok   yaml valide (aucune cle dupliquee)")
        except Exception as exc:  # noqa: BLE001
            failures += 1
            print(f"   FAIL yaml: {exc}")
            continue
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
