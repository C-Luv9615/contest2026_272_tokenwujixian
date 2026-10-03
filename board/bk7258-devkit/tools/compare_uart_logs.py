#!/usr/bin/env python3
"""Compare two BK7258 UART logs on the key events, per spec S2.8.

Phase one compiles Armino's own sources, so their logs (wifid, wpa, rwnx,
hitf, lwip) print unchanged in our firmware. That makes the two UART logs
directly comparable: extract the event sequence and counts from each and
report the difference. No register dumps, no instrumentation.

Events (S2.8): scan start and completion, auth, assoc, every 4-way
handshake state, DHCP, re-handshake count, tx_dma_dead, assert, restart.

Usage:
    compare_uart_logs.py REFERENCE CANDIDATE

Exit status is 1 when the candidate has more re-handshakes or more
tx_dma_dead than the reference -- the two counters T11 gates on. Asserts
and restarts are reported but do not set the exit status. It is a report
tool, not a pass/fail gate for anything else.
"""

from __future__ import annotations

import argparse
import collections
import pathlib
import re
import sys

# Counted once per occurrence. Each maps to the S2.8 item it serves.
PATTERNS: dict[str, str] = {
    "scan_start": r"scanu_start_req",
    "scan_done": r"scanu_confirm",
    "auth_send": r"auth_send:",
    "assoc_req": r"assoc_req_send",
    "assoc_rsp": r"assoc_rsp",
    "rehandshake": r"State: COMPLETED -> 4WAY_HANDSHAKE",
    "handshake_done": r"State: GROUP_HANDSHAKE -> COMPLETED",
    "dhcp": r"(me dhcp done|wlan0 ip |dhcp done|inet addr:(?!0\.0\.0\.0))",
    "tx_dma_dead": r"tx_dma_dead",
    "assert": r"(Assertion failed|PANIC|dump_assert_info)",
    # Two markers per boot (the !RST banner and the PSRAM line); take the
    # larger so one restart is not counted twice.
    "restart": r"!RST ",
}

BOOT_BANNER_RE = re.compile(r"\[BK7258\] PSRAM id=")

# T11 gates on these two: "重握手次数、tx_dma_dead 次数不多于 Armino 参照".
# assert and restart are reported but do not set the exit status -- a
# capture that starts at a reset carries its own !RST banner, which is a
# window artifact rather than a regression (T1's log is such a case).
REGRESSION_KEYS = ("rehandshake", "tx_dma_dead")

STATE_RE = re.compile(r"State: ([A-Z_0-9]+) -> ([A-Z_0-9]+)")


def parse(path: pathlib.Path) -> dict[str, object]:
    text = path.read_text(encoding="utf-8", errors="replace")
    counts: collections.Counter[str] = collections.Counter()
    for name, pattern in PATTERNS.items():
        counts[name] = len(re.findall(pattern, text))
    counts["restart"] = max(counts["restart"], len(BOOT_BANNER_RE.findall(text)))

    states = [f"{a} -> {b}" for a, b in STATE_RE.findall(text)]
    counts["state_transitions"] = len(states)
    return {"counts": counts, "states": states, "lines": text.count("\n") + 1}


def report(ref: dict[str, object], cand: dict[str, object],
           ref_name: str, cand_name: str) -> bool:
    """Print the comparison; return True when the candidate is worse."""

    rc: collections.Counter[str] = ref["counts"]
    cc: collections.Counter[str] = cand["counts"]

    print(f"reference: {ref_name}  ({ref['lines']} lines)")
    print(f"candidate: {cand_name}  ({cand['lines']} lines)")
    print()
    print(f"{'event':<18}{'ref':>6}{'cand':>7}   note")
    print("-" * 52)

    worse = False
    for key in PATTERNS:
        r, c = rc[key], cc[key]
        note = ""
        if key in REGRESSION_KEYS:
            if c > r:
                note = "REGRESSION"
                worse = True
            elif c < r:
                note = "better"
        elif c != r:
            note = "differs"
        print(f"{key:<18}{r:>6}{c:>7}   {note}")

    print()
    print(f"{'state_transitions':<18}{rc['state_transitions']:>6}"
          f"{cc['state_transitions']:>7}")

    # The 4-way sequence is where re-handshakes show up; show it in order.
    def handshake_seq(states: list[str]) -> list[str]:
        keep = ("SCANNING", "ASSOCIATING", "ASSOCIATED", "4WAY_HANDSHAKE",
                "GROUP_HANDSHAKE", "COMPLETED", "DISCONNECTED")
        return [s for s in states
                if any(k in s for k in keep)]

    print()
    print("handshake sequence (reference):")
    for s in handshake_seq(ref["states"]):
        print(f"  {s}")
    print("handshake sequence (candidate):")
    for s in handshake_seq(cand["states"]):
        print(f"  {s}")

    only_ref = [s for s in ref["states"] if s not in cand["states"]]
    only_cand = [s for s in cand["states"] if s not in ref["states"]]
    if only_ref or only_cand:
        print()
        print("transitions only in reference:")
        for s in only_ref:
            print(f"  {s}")
        print("transitions only in candidate:")
        for s in only_cand:
            print(f"  {s}")

    print()
    if worse:
        print("candidate is worse on a tracked counter")
    else:
        print("candidate is not worse on any tracked counter")
    return worse


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=pathlib.Path)
    parser.add_argument("candidate", type=pathlib.Path)
    args = parser.parse_args()

    for path in (args.reference, args.candidate):
        if not path.is_file():
            print(f"error: no such log: {path}", file=sys.stderr)
            return 2

    ref = parse(args.reference)
    cand = parse(args.candidate)
    worse = report(ref, cand, str(args.reference), str(args.candidate))
    return 1 if worse else 0


if __name__ == "__main__":
    sys.exit(main())
