# Armino baseline import (T19)

Byte-identical import of the Armino sources phase one compiles, so a clean
checkout builds with no local Armino tree and no `BK7258_ARMINO_BASELINE_DIR`.

## Provenance

| | |
|---|---|
| Source tree | Armino BK7258 CP SDK (`cp/`), plus its iperf-generated config |
| Baseline commit | `d5b0278ea1b256a5c6b8a4f98b88509adc9c679d` (also the header of `../tools/armino_baseline/component_flags.tsv`) |
| Local checkout | `$HOME/armino/.worktrees/baseline-gcc13` at import time (2026-10-06) |
| Specification | `docs/compose/spec/bk7258-wifi-native.md` T19, rules S2.2 / S2.3 |

Structure under this directory mirrors the baseline (`cp/`, `build/`,
`projects/`), so `@ARMINO@` in the manifest keeps pointing at the same
relative paths. Nothing here is edited: every `.c`/`.h`/`.S` is byte-identical
to the baseline.

## What is included

- the 470 Armino units kept by phase one (`../tools/armino_baseline/armino_units.tsv`,
  minus the S2.7 exclusions),
- **every header under every `-I` directory** those units compile with (130
  dirs, ~1600 headers), plus the generated partition/config headers
  (`build/bk7258/iperf/bk7258/config/sdkconfig.h`,
  `build/bk7258/iperf/partitions/partitions_gen.h`,
  `build/bk7258/iperf/bk7258/armino/partitions/_build/*`),
- the six closed archives on the baseline link line
  (`cp/components/bk_libs/bk7258/libs/`):

  | archive | sha256 |
  |---|---|
  | `libwifi.a` | `6e7e018c884674ce…` |
  | `libbk_phy.a` | `fb3e8f6a6782f6fa…` |
  | `libbk_coex.a` | `2c6b804fb2834bd1…` |
  | `libbk_phy_info.a` | `db880d5a1ece55b2…` |
  | `libble_wifi_exchange.a` | `ae48788112657797…` |
  | `libuid.a` | `aaab2dd17e69183c…` |

Full digests are recorded below. `build/` here is the baseline's generated
config, not a build output; the rest of the baseline build tree is not
imported.

Why "every header under the include dirs" rather than only the ones the
compiler reports: the compiler's dependency list is not a complete record of
what it opens. `cp/components/bk_pm/pm_model.c` line 2 is
`#include "arm_arch.h"` and the old build resolves it at
`cp/middleware/driver/include/bk_private/legacy/arm_arch.h`, but that header
appears neither in the ninja dep log nor in `gcc -M`/`-H` output for that
unit (it is listed when it is the first include of a translation unit).
Trusting the dep list alone therefore imported an incomplete tree and the
build failed with `arm_arch.h: No such file`. Taking all headers under the
include dirs removes that class of error.

## What is excluded

Everything phase one does not compile (S2.7): `os_source` (FreeRTOS kernel —
NuttX owns the scheduler), the lwip protocol code (NuttX owns the stack; only
the six pbuf/mem/def/ip4_addr/stats units stay, and they live in the baseline
tree paths listed in the manifest), the Bluetooth controller archives, demos,
and the units named in `wifi_armino/CMakeLists.txt` `excluded_units`. The
whole trees those belong to are not imported.

## Verification

Byte identity is gated by

```sh
python3 chips/bk7258/tools/armino_source_gate.py \
  --tree chips/bk7258/armino_baseline
```

which maps `cp/…`, `build/…`, `projects/…` onto the same relative paths in a
baseline checkout and compares file by file (2058 `.c`/`.h`/`.S` files
compared). T19 acceptance also requires a build with no
`BK7258_ARMINO_BASELINE_DIR` to produce an unchanged image: `.text`
disassembly matches except for objdump's own file path, and the
debug-stripped `nuttx.elf` matches the pre-import build byte for byte
(CP `3281ae83…`, AP `ee4fe515…`, 2026-10-06).

Regenerating the manifest (`armino_compile_manifest.py`) still needs a real
baseline checkout; `BK7258_ARMINO_BASELINE_DIR` remains an override for that.
Renew the digests here when the baseline commit changes.

## Closed archive digests

```
2c6b804fb2834bd1ec08f9caad70a4dec808c3c7ed74e4bb84624dc75eb244fd  libbk_coex.a
fb3e8f6a6782f6fab6edde59ec95417c0da1da2ab8263230848535b34e4a6979  libbk_phy.a
db880d5a1ece55b2249423c252974d01c159fef57ac2433acc4e0642bd233396  libbk_phy_info.a
ae487881126577979d2ea899c83ed02c9ad3a61ac148ab6604f6a7f1f2111f96  libble_wifi_exchange.a
aaab2dd17e69183c897bd637d2565ab5c8336e28948f5889cd5e0a186fe9a4ff  libuid.a
6e7e018c884674ced3305d241138eae004c4c3d8568123573c24ec9c71d37b0c  libwifi.a
```
