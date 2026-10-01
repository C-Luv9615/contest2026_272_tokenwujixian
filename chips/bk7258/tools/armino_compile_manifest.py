#!/usr/bin/env python3
"""Generate the phase-one Armino compile manifest from the baseline build.

Spec: docs/compose/spec/bk7258-wifi-native.md, task T4 (rule S2.2).

Reads the clean Armino iperf CP build's compile_commands.json and emits:

  armino_units.tsv   every compile unit: component, source (repo-relative),
                     object name, -D__FILE__ value
  component_flags.tsv  one row per component: the full flag set shared by all
                     of its units, with -I/-include paths rewritten from the
                     baseline tree to the workspace tree prefix

The generator is deterministic; --check fails when the committed files differ
from a fresh run. Excluding a component (S2.7) happens in the CMake that
consumes this manifest, not here: the manifest always describes all units so
the exclusion decision stays visible and reversible.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path


DEFAULT_BUILD = Path.home() / "armino/.worktrees/baseline-gcc13/build/bk7258/iperf/bk7258"
DEFAULT_OUT = Path(__file__).resolve().parent / "armino_baseline"

SKIP_WITH_ARG = ("-o", "-MF", "-MT", "-c")
OBJ_TOKEN = re.compile(r"\.obj$")
FILE_DEFINE = "-D__FILE__="


def split_flags(command: str, source: str) -> list[str]:
    """Extract compile flags, dropping per-file tokens (-o, -MF/-MT, __FILE__, source)."""
    tokens = command.split()
    flags: list[str] = []
    skip = False
    for tok in tokens[1:]:
        if skip:
            skip = False
            continue
        if tok in SKIP_WITH_ARG:
            if tok != "-c":
                skip = True
            continue
        if tok in ("-MD", source):
            continue
        if tok.startswith(FILE_DEFINE):
            continue
        if OBJ_TOKEN.search(tok) and "/" in tok and not tok.startswith("-"):
            continue
        flags.append(tok)
    return flags


def generate(build: Path, root: Path) -> dict[str, str]:
    root = root.resolve()
    commands = json.loads((build / "compile_commands.json").read_text())

    units = []
    per_component: dict[str, set[str]] = {}
    file_defines: dict[str, str] = {}
    for entry in commands:
        source = os.path.realpath(entry["file"])
        out = re.search(r"-o (\S+)", entry["command"]).group(1)
        if not out.startswith("armino/"):
            continue  # the link-helper unit; not a component source
        component = re.match(r"armino/([^/]+)/", out).group(1)
        rel_source = os.path.relpath(source, root)
        obj = Path(out).name.removesuffix(".obj")
        units.append((component, rel_source, obj))

        define = re.search(r"(-D__FILE__=\\\"[^\\]*\\\")", entry["command"])
        file_defines[(component, rel_source)] = define.group(1) if define else "-"
        flags = split_flags(entry["command"], source)
        per_component.setdefault(component, set()).add(" ".join(flags))

    for component, sets in per_component.items():
        if len(sets) != 1:
            raise SystemExit(f"{component}: flag sets differ between units; "
                             "the manifest model no longer holds")

    def workspace(flag: str) -> str:
        """Rewrite baseline-tree paths to the workspace prefix @ARMINO@."""
        return flag.replace(str(root) + "/", "@ARMINO@/")

    units.sort()
    tsv = ["component\tsource\tobject\t__FILE__"]
    tsv += [f"{c}\t{s}\t{o}\t{file_defines[(c, s)]}" for c, s, o in units]

    flags_rows = ["component\tflags"]
    for component in sorted(per_component):
        flags = next(iter(per_component[component])).split()
        flags_rows.append(f"{component}\t" + " ".join(workspace(f) for f in flags))

    return {"armino_units.tsv": "\n".join(tsv) + "\n",
            "component_flags.tsv": "\n".join(flags_rows) + "\n"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    build = args.build.resolve()
    root = build.parents[3]
    if not (root / "cp").is_dir():
        raise SystemExit(f"not an Armino build directory: {build}")
    head = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"],
                          check=True, capture_output=True, text=True).stdout.strip()

    files = generate(build, root)
    files["component_flags.tsv"] = (f"# baseline {head}\n" + files["component_flags.tsv"])
    if args.check:
        stale = [n for n, text in files.items()
                 if not (args.out / n).is_file() or (args.out / n).read_text() != text]
        for n in stale:
            print(f"stale: {args.out / n}", file=sys.stderr)
        return 1 if stale else 0
    args.out.mkdir(parents=True, exist_ok=True)
    for name, text in files.items():
        (args.out / name).write_text(text)
    units = files["armino_units.tsv"].count("\n") - 1
    components = files["component_flags.tsv"].count("\n") - 2
    print(f"wrote {units} units across {components} components to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
