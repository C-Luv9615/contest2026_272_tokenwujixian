#!/usr/bin/env python3
"""Gate: vendored Armino sources must be byte-identical to the baseline.

Spec: docs/compose/spec/bk7258-wifi-native.md, task T3 (rule S2.3).

Each file under --tree is mapped to a baseline path by the longest matching
prefix in --map, then compared byte for byte. A file is reported as
"modified" when the baseline file exists but differs, and "no-counterpart"
when no prefix matches or the mapped baseline file does not exist. Paths
listed in --exceptions (one tree-relative path per line, '#' comments) are
reported but do not fail the gate. The exception list is meant to be empty.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_TREE = ROOT / "chips/bk7258/wifi/armino"
DEFAULT_BASELINE = Path.home() / "armino/.worktrees/baseline-gcc13"
# tree prefix -> baseline prefix, checked longest first.
DEFAULT_MAP = (
    "glue/=cp/components/",
    "wpa_supplicant/=cp/components/wpa_supplicant-2.10/",
    "board/=cp/middleware/boards/",
    "include/common/=cp/components/bk_common/include/",
    "include/=cp/include/",
)
SUFFIXES = (".c", ".h", ".S")


def parse_map(items: list[str]) -> list[tuple[str, str]]:
    rules = []
    for item in items:
        src, sep, dst = item.partition("=")
        if not sep:
            raise SystemExit(f"bad --map entry (want TREE=BASELINE): {item}")
        rules.append((src, dst))
    return sorted(rules, key=lambda r: len(r[0]), reverse=True)


def baseline_path(rel: str, rules: list[tuple[str, str]], baseline: Path) -> Path | None:
    for src, dst in rules:
        if rel.startswith(src):
            return baseline / (dst + rel[len(src):])
    return None


def check(tree: Path, baseline: Path, rules, exceptions: set[str]):
    result = {"identical": [], "modified": [], "no-counterpart": []}
    for path in sorted(p for p in tree.rglob("*") if p.is_file() and p.suffix in SUFFIXES):
        rel = path.relative_to(tree).as_posix()
        ref = baseline_path(rel, rules, baseline)
        if ref is None or not ref.is_file():
            result["no-counterpart"].append(rel)
        elif ref.read_bytes() != path.read_bytes():
            result["modified"].append(rel)
        else:
            result["identical"].append(rel)
    failing = [r for r in result["modified"] + result["no-counterpart"] if r not in exceptions]
    return result, failing


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tree", type=Path, default=DEFAULT_TREE)
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--map", action="append", default=None,
                        help="TREE_PREFIX=BASELINE_PREFIX (repeatable)")
    parser.add_argument("--exceptions", type=Path)
    parser.add_argument("--quiet", action="store_true", help="print counts only")
    args = parser.parse_args()

    if not (args.baseline / "cp").is_dir():
        raise SystemExit(f"not an Armino tree: {args.baseline}")
    rules = parse_map(args.map or list(DEFAULT_MAP))
    exceptions: set[str] = set()
    if args.exceptions:
        for line in args.exceptions.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                exceptions.add(line)

    result, failing = check(args.tree, args.baseline, rules, exceptions)
    for kind in ("modified", "no-counterpart"):
        by_suffix = {s: sum(r.endswith(s) for r in result[kind]) for s in SUFFIXES}
        print(f"{kind}: {len(result[kind])} " +
              " ".join(f"{s}={n}" for s, n in by_suffix.items()))
        if not args.quiet:
            for rel in result[kind]:
                mark = " (exception)" if rel in exceptions else ""
                print(f"  {rel}{mark}")
    print(f"identical: {len(result['identical'])}")
    print("gate: " + ("fail" if failing else "pass"))
    return 1 if failing else 0


if __name__ == "__main__":
    sys.exit(main())
