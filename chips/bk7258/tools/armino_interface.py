#!/usr/bin/env python3
"""Extract the BK7258 Armino baseline interface table.

Spec: docs/compose/spec/bk7258-wifi-native.md, task T2.

Reads the clean Armino iperf CP build (app.map cross reference,
compile_commands.json, the --wrap flags in build.ninja) and the closed Wi-Fi
archives it links, then writes two files:

  interface.tsv  every symbol defined in one component and referenced from
                 another: symbol, provider component, provider object,
                 provider source, consumer components
  summary.txt    the closed-library external symbols and their providers, the
                 S2.7 candidate-component table and the S2.5 hardware objects

--check regenerates in memory and exits non-zero if the files on disk differ.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


DEFAULT_BUILD = Path.home() / "armino/.worktrees/baseline-gcc13/build/bk7258/iperf/bk7258"
DEFAULT_OUT = Path(__file__).resolve().parent / "armino_baseline"

# The closed archives whose external symbols define the Wi-Fi contract (S2.5).
CLOSED_WIFI = ("prebuilt:libwifi", "prebuilt:libbk_phy")

# Component classes for the S2.5 reachability walk. These are design inputs:
# closed blobs, Armino open logic reached on the Wi-Fi side, and hardware.
CLOSED = {"prebuilt:libwifi", "prebuilt:libbk_phy", "prebuilt:libbk_coex",
          "prebuilt:libbk_phy_info", "prebuilt:libuid"}
LOGIC = {"bk_wifi", "wpa_supplicant-2.10", "bk_phy", "controller_if", "bk_event",
         "vnd_cal", "temp_detect", "bk_adapter", "bk_common", "hmac_sha_256",
         "bk_netif", "bk_system", "psa_mbedtls", "base64", "utf8", "bk_uid",
         "easy_flash", "bk_ate"}
HARDWARE = {"driver", "bk7258", "common", "cm33", "cmsis", "bk_pm"}

# S2.7 exclusion candidates.
CANDIDATES = ("os_source", "bk_rtos", "lwip_intf_v2_1", "cm33", "cmsis", "bk_startup")

ENTRY = re.compile(r"^(?P<archive>.+?\.a)\((?P<member>[^)]+)\)$")


def component(archive: str) -> str:
    m = re.match(r"armino/([^/]+)/", archive)
    if m:
        return m.group(1)
    name = Path(archive).name.removesuffix(".a")
    if "/components/bk_libs/" in archive:
        return "prebuilt:" + name
    if "arm-none-eabi" in archive:
        return "toolchain:" + name
    return "other:" + name


def split_entry(entry: str) -> tuple[str, str]:
    """Map a map-file file column to (component, object name)."""
    m = ENTRY.match(entry)
    if m:
        return component(m["archive"]), m["member"]
    return "object", Path(entry).name


def parse_xref(map_text: str):
    """Return {symbol: definer} and {symbol: [referrers]} from the cross reference."""
    lines = map_text.split("Cross Reference Table", 1)[1].splitlines()
    definer: dict[str, tuple[str, str]] = {}
    refs: dict[str, list[tuple[str, str]]] = collections.defaultdict(list)
    sym = None
    pending = False
    for line in lines:
        if not line.strip():
            continue
        if not line[0].isspace():
            parts = line.split()
            if parts == ["Symbol", "File"]:
                continue
            sym = parts[0]
            pending = len(parts) == 1
            if not pending:
                definer[sym] = split_entry(parts[1])
            continue
        obj = split_entry(line.strip())
        if pending:
            definer[sym] = obj
            pending = False
        else:
            refs[sym].append(obj)
    return definer, refs


def nm_archive(nm: str, archive: str):
    """Return (defined, {undefined symbol: [members]}) for one archive."""
    out = subprocess.run([nm, "-A", "-g", archive], check=True,
                         capture_output=True, text=True).stdout
    defined: set[str] = set()
    undefined: dict[str, list[str]] = collections.defaultdict(list)
    for line in out.splitlines():
        # Defined: "lib.a:obj.o:00000000 T sym"; undefined: "lib.a:obj.o:         U sym".
        m = re.match(r"^.+?\.a:([^:]+):\s*([0-9a-f]+\s+)?([A-Za-z])\s+(\S+)$", line)
        if not m:
            continue
        member, addr, _, sym = m.groups()
        if addr:
            defined.add(sym)
        else:
            undefined[sym].append(member)
    return defined, undefined


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate(build: Path, nm: str) -> dict[str, str]:
    root = build.parents[3]
    if not (root / "cp").is_dir():
        raise SystemExit(f"not an Armino build directory: {build}")
    map_text = (build / "app.map").read_text(errors="ignore")
    commands = json.loads((build / "compile_commands.json").read_text())
    wraps = set(re.findall(r"--wrap=([A-Za-z0-9_]+)", (build / "build.ninja").read_text()))

    source: dict[tuple[str, str], str] = {}
    units: collections.Counter[str] = collections.Counter()
    for entry in commands:
        out = re.search(r" -o (\S+)", entry["command"]).group(1)
        m = re.match(r"armino/([^/]+)/", out)
        comp = m.group(1) if m else "object"
        units[comp] += 1
        source[(comp, Path(out).name)] = os.path.relpath(
            os.path.realpath(entry["file"]), root)

    definer, refs = parse_xref(map_text)
    linked = {(a, mem) for a, mem in re.findall(r"(\S+\.a)\(([^)\s]+)\)", map_text)}
    archives = sorted({a for a, _ in linked})

    # interface.tsv
    rows = []
    for sym, d in definer.items():
        consumers = sorted({c for c, _ in refs[sym] if c != d[0]})
        if consumers:
            rows.append((d[0], sym, d[1], source.get(d, "-"), ",".join(consumers)))
    rows.sort()
    tsv = ["symbol\tprovider_component\tprovider_object\tprovider_source\tconsumers"]
    tsv += [f"{sym}\t{comp}\t{obj}\t{src}\t{cons}" for comp, sym, obj, src, cons in rows]

    # Closed-library external symbols.
    closed_paths = [a for a in archives if component(a) in CLOSED_WIFI]
    defined: set[str] = set()
    undefined: dict[str, list[tuple[str, str]]] = collections.defaultdict(list)
    for a in closed_paths:
        d, u = nm_archive(nm, a)
        defined |= d
        for sym, members in u.items():
            undefined[sym] += [(a, m) for m in members]
    external = sorted(set(undefined) - defined)

    summary = ["# BK7258 Armino baseline interface summary",
               f"armino HEAD: {git_head(root)}"]
    for a in closed_paths:
        summary.append(f"{os.path.relpath(a, root)} sha256 {sha256(Path(a))}")

    resolved, notes = [], []
    for sym in external:
        if sym in definer:
            d = definer[sym]
            resolved.append(f"  {sym}\t{d[0]}\t{d[1]}\t{source.get(d, '-')}")
        elif sym in wraps and "__wrap_" + sym in definer:
            d = definer["__wrap_" + sym]
            notes.append(f"  {sym}\tvia --wrap -> __wrap_{sym}\t{d[0]}\t{d[1]}\t{source.get(d, '-')}")
        else:
            users = sorted({m for a, m in undefined[sym]})
            unlinked = [m for m in users
                        if not any((a, m) in linked for a in closed_paths)]
            state = "not linked" if len(unlinked) == len(users) else "LINKED"
            notes.append(f"  {sym}\treferenced only by {','.join(users)} ({state})")
    summary += ["", f"## closed external symbols: {len(external)}, "
                f"with a cross-reference provider: {len(resolved)}"]
    summary += resolved
    summary += ["", f"## closed external symbols without a cross-reference entry: {len(notes)}"]
    summary += notes

    # S2.7 candidate components.
    summary += ["", "## S2.7 candidates: component, units, used exports, callers"]
    for comp in CANDIDATES:
        used: dict[str, set[str]] = {}
        for sym, d in definer.items():
            if d[0] == comp:
                callers = {c for c, _ in refs[sym] if c != comp}
                if callers:
                    used[sym] = callers
        callers = collections.Counter(c for cs in used.values() for c in cs)
        top = sorted(callers.items(), key=lambda kv: (-kv[1], kv[0]))
        summary.append(f"  {comp}\t{units[comp]}\t{len(used)}\t"
                       + ", ".join(f"{c} {n}" for c, n in top))

    # S2.5 hardware objects reached from the closed libraries.
    dep: dict[tuple[str, str], set[tuple[str, str]]] = collections.defaultdict(set)
    for sym, d in definer.items():
        for r in refs[sym]:
            if r != d:
                dep[r].add(d)
    nodes = set(dep) | {d for ds in dep.values() for d in ds}
    side = {o for o in nodes if o[0] in CLOSED}
    stack = list(side)
    while stack:
        for d in dep.get(stack.pop(), ()):
            if d not in side and (d[0] in LOGIC or d[0] in CLOSED):
                side.add(d)
                stack.append(d)
    direct = {d for o in side for d in dep.get(o, ()) if d[0] in HARDWARE}
    hardware = set(direct)
    stack = list(direct)
    while stack:
        for d in dep.get(stack.pop(), ()):
            if d[0] in HARDWARE and d not in hardware:
                hardware.add(d)
                stack.append(d)
    summary += ["", f"## S2.5 hardware objects: {len(hardware)}, direct: {len(direct)}"]
    for o in sorted(hardware):
        kind = "direct" if o in direct else "indirect"
        summary.append(f"  {kind}\t{o[0]}\t{o[1]}\t{source.get(o, '-')}")

    return {"interface.tsv": "\n".join(tsv) + "\n",
            "summary.txt": "\n".join(summary) + "\n"}


def git_head(root: Path) -> str:
    return subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"],
                          check=True, capture_output=True, text=True).stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build", type=Path, default=DEFAULT_BUILD,
                        help="Armino iperf CP build directory")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--nm", default=shutil.which("arm-none-eabi-nm"),
                        help="arm-none-eabi-nm (default: from PATH)")
    parser.add_argument("--check", action="store_true",
                        help="fail if the files in --out differ from a fresh run")
    args = parser.parse_args()
    if not args.nm:
        raise SystemExit("arm-none-eabi-nm not found; pass --nm")

    files = generate(args.build.resolve(), args.nm)
    if args.check:
        stale = [n for n, text in files.items()
                 if not (args.out / n).is_file() or (args.out / n).read_text() != text]
        for n in stale:
            print(f"stale: {args.out / n}", file=sys.stderr)
        return 1 if stale else 0
    args.out.mkdir(parents=True, exist_ok=True)
    for name, text in files.items():
        (args.out / name).write_text(text)
    print(f"wrote {', '.join(sorted(files))} to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
