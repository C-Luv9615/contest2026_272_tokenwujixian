#!/usr/bin/env python3
"""Host tests for the FreeRTOS -> NuttX seam, T9.

Compiles the real chips/bk7258/wifi_armino/compat/bk7258_freertos_nuttx.c
against the stub NuttX headers in tests/os_seam/include, links it with
tests/os_seam/harness.c, and runs the four checks the sdk-source branch got
wrong: queue item_size, stack depth in words, critical-section nesting, and
queue send from interrupt context.

Host evidence only; it does not claim anything about the board.
"""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]
TESTS = pathlib.Path(__file__).resolve().parent
SEAM = ROOT / "chips" / "bk7258" / "wifi_armino" / "compat" / "bk7258_freertos_nuttx.c"
HARNESS = TESTS / "os_seam" / "harness.c"
STUBS = TESTS / "os_seam" / "include"


def _compiler() -> str:
    for name in ("gcc", "cc", "clang"):
        if shutil.which(name):
            return name
    raise unittest.SkipTest("no host C compiler available")


def build_and_run() -> tuple[int, str]:
    """Compile the seam and the harness, run the result, return its output."""

    cc = _compiler()
    with tempfile.TemporaryDirectory(prefix="os-seam-") as tmp:
        out = pathlib.Path(tmp) / "os-seam-test"
        objects = []
        for source in (SEAM, HARNESS):
            obj = pathlib.Path(tmp) / (source.stem + ".o")
            subprocess.run(
                [
                    cc,
                    "-std=gnu99",
                    "-Wall",
                    "-Werror",
                    "-Wno-unused-parameter",
                    f"-I{STUBS}",
                    "-c",
                    str(source),
                    "-o",
                    str(obj),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            objects.append(str(obj))

        subprocess.run(
            [cc, *objects, "-o", str(out)],
            check=True,
            capture_output=True,
            text=True,
        )
        run = subprocess.run([str(out)], capture_output=True, text=True)
        return run.returncode, run.stdout + run.stderr


class Bk7258OsSeamTest(unittest.TestCase):
    """The four T9 items, executed on the host."""

    @classmethod
    def setUpClass(cls) -> None:
        if not SEAM.is_file():
            raise unittest.SkipTest(f"seam source missing: {SEAM}")
        if not HARNESS.is_file():
            raise unittest.SkipTest(f"harness missing: {HARNESS}")
        cls.rc, cls.output = build_and_run()

    def test_all_checks_pass(self) -> None:
        self.assertEqual(
            self.rc,
            0,
            f"os-seam checks failed:\n{self.output}",
        )

    def test_queue_item_size(self) -> None:
        self.assertIn("queue item_size: 16 bytes round-tripped unchanged",
                      self.output)

    def test_stack_depth_is_in_words(self) -> None:
        self.assertIn("stack words: 32 words became 32*4+1024 bytes",
                      self.output)

    def test_critical_section_nests(self) -> None:
        self.assertIn("critical nesting: one restore for two levels, from the first save",
                      self.output)

    def test_send_from_isr_never_blocks(self) -> None:
        self.assertIn("isr send: full queue answered without blocking",
                      self.output)
        self.assertIn("isr send: no critical section taken", self.output)


if __name__ == "__main__":
    unittest.main()
