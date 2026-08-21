# BK7258 Platform Adaptation Plan

## 1. Purpose

This document is the handoff plan for implementing the BK7258 platform
peripherals that the Wi-Fi runtime depends on.

The work is intentionally separated from the Wi-Fi integration itself:

```text
BK7258 platform drivers  ->  reusable chips/bk7258 APIs
                                      |
                                      v
                         chips/bk7258/wifi integration
                                      |
                                      v
                         libwifi.a / libbk_phy.a
```

The platform branch must not depend on:

- `chips/bk7258/wifi/`;
- `third_party/beken_armino/`;
- `libwifi.a`, `libbk_phy.a`, or `libbk_phy_info.a`;
- WPA or Wi-Fi Kconfig symbols.

Armino is used as the hardware-contract reference. The implementation target
is the OpenVela/NuttX chip and board framework.

## 2. Current repository state

### Mainline

The local `dev` branch has been fetched and rebased onto the upstream OpenVela
branch:

```text
remote: openvela
branch: dev-ai-contest-2026
HEAD: 5c1e200
```

The upstream update includes the recent BK7258 SPI LCD work. The main checkout
also contains user-owned local logs and the Wi-Fi porting plan; those changes
were preserved during the rebase and are not part of this platform branch.

### Platform worktree

```text
path:   /home/czp/openvela_contest/.worktrees/bk7258-platform
branch: feature/bk7258-platform
HEAD:   bdbd215
base:   dev at 5c1e200
```

The platform worktree is currently clean and contains:

- existing BK7258 chip implementations for startup, heap, timer, clock, IRQ,
  UART, GPIO, and Mailbox;
- `BK7258_PLATFORM_PERIPHERAL_MATRIX.md`;
- this handoff plan.

### Wi-Fi worktree

```text
path:   /home/czp/openvela_contest/.worktrees/bk7258-wifi
branch: feature/bk7258-wifi
HEAD:   e676d0d
```

The Wi-Fi worktree contains the vendored Armino glue, WPA source set, NuttX
OSAL, RF calibration data, and the real hard-float link PoC. It also contains
uncommitted follow-up changes from the link-closure experiment. It is not the
platform development tree and must not be used as the base for platform work.

## 3. Platform stages

Each stage is implemented and verified independently, then merged into `dev`.
The next stage starts from the updated `dev`, not from an old platform branch.

### P0 — Clock, reset, IRQ, GPIO and pinmux

Scope:

- MAC/PHY/modem clock enable and reset;
- Wi-Fi ICU source routing and NuttX IRQ attachment;
- TXEN/RXEN and board PA/LNA GPIO mux;
- readback and error handling.

Armino references:

- `middleware/soc/bk7258/soc/sys_reg.h`;
- `middleware/soc/bk7258/soc/icu_map.h`;
- `middleware/soc/bk7258/hal/sys_hal.c` and `sys_ll.h`;
- `middleware/driver/sys_ctrl/sys_wifi_driver.c`;
- `middleware/driver/bk7258/gpio_driver.h` and GPIO HAL types.

Existing team code:

- `chips/bk7258/bk7258_clock.c`;
- `chips/bk7258/bk7258_irq.c`;
- `chips/bk7258/bk7258_gpio.c`;
- `chips/bk7258/include/bk7258_memorymap.h`;
- `chips/bk7258/include/irq.h`.

Acceptance:

- normal CP build succeeds with Wi-Fi disabled;
- clock and reset register readback matches the contract;
- ICU 29–38 map to the documented NVIC IRQs;
- GPIO mux changes are observable by readback or an explicit target test;
- no Wi-Fi source or prebuilt library is needed.

### P1 — AON PMU and chip revision

Scope:

- AON PMU register base and access sequence;
- `aon_pmu_hal_get_chipid()`;
- `aon_pmu_hal_reg_get()`;
- `aon_pmu_drv_get_adc_cal()`;
- `aon_pmu_drv_bias_cal_get()`;
- chip revision matching.

Acceptance:

- chip ID is read from hardware, not returned as zero;
- the value matches one of the known revision masks;
- invalid/read-failure paths are explicit;
- Wi-Fi can consume the platform API without including Armino headers.

This stage removes the first ABI_AUDIT blocker. Until it is complete, the
Wi-Fi shim's zero-return PMU functions must remain marked as blocking.

### P2 — Analog register transaction layer

Scope:

- port the `sys_set_ana_reg_bit()` latch/unlatch sequence;
- implement read/write/update-bits helpers;
- implement analog setters for VCORE, ALDO/DLDO, bandgap, IO LDO, crystal
  fine-tune and ADC divider;
- preserve critical-section and timing requirements.

Armino references:

- `middleware/soc/bk7258/hal/sys_ll.h`;
- `middleware/soc/bk7258/hal/sys_pm_hal.c`;
- `middleware/driver/sys_ctrl/sys_driver.c`;
- `middleware/driver/sys_ctrl/sys_wifi_driver.c`.

Important constraint: these are not ordinary MMIO writes. A guessed
`putreg32()` is not an implementation.

Acceptance:

- every register update has a readback or hardware-observable test;
- invalid masks and unavailable analog domains fail explicitly;
- negative test proves a disabled/invalid write is rejected;
- no analog shim remains a silent no-op.

This stage removes the analog-register blocker from ABI_AUDIT.

### P3 — OTP and eFuse

Scope:

- team-owned OTP/eFuse map;
- read MAC, RF trim, bandgap, temperature and ADC calibration items;
- reject writes unless the product explicitly authorizes irreversible OTP
  programming;
- map generated Armino OTP identifiers to the product-owned map.

Acceptance:

- valid reads return known target values;
- invalid item IDs fail without modifying output buffers;
- write attempts are rejected by default;
- the map is documented as a product contract, not borrowed indefinitely from
  the genie build artifact.

This stage removes the OTP blocker from ABI_AUDIT.

### P4 — SARADC, temperature and voltage

Scope:

- ADC acquire/init/config/start/read/stop/release;
- ADC calibration and raw-to-voltage conversion;
- temperature sensor initialization and reading;
- voltage monitor used by PHY calibration;
- timeout and sample validity semantics.

Armino references:

- `middleware/driver/saradc/adc_driver.c`;
- `middleware/soc/common/hal/adc_hal.h`;
- `components/temp_detect/temp_detect.h`;
- `components/temp_detect/bk_sensor.c`;
- `components/temp_detect/temp_detect_pub.h`.

Acceptance:

- repeated raw samples are stable within documented tolerance;
- units and calibration are documented;
- timeout and unavailable-sensor paths fail explicitly;
- no fake temperature or voltage is passed to PHY calibration.

### P5 — Flash/MTD and RF calibration partition

Scope:

- add a product-owned RF calibration region to the board packaging/profile;
- implement read/write/erase through NuttX MTD/progmem;
- implement partition lookup and bounds checking;
- preserve blank-region and power-loss-safe behavior.

Armino reference data:

- genie generated artifact: `SYS_RF` at `0x007fe000`, size `0x1000`;
- this value must not be copied blindly because the team board's partition
  profile currently has no RF partition.

Acceptance:

- erase/write/readback succeeds within the declared partition;
- out-of-bounds access fails;
- blank or invalid calibration data is detected;
- the packaging profile and runtime partition map use one source of truth.

This stage removes the RF calibration storage blocker from ABI_AUDIT.

### P6 — DMA and cache

Scope:

- BK7258 GDMA channel and ownership;
- alignment and descriptor requirements;
- cache clean/invalidate hooks;
- barriers between CPU and MAC DMA;
- TX/RX buffer lifetime.

Acceptance:

- aligned and deliberately misaligned transfers are tested;
- cache-enabled transfer test passes;
- descriptor ownership and release order are documented;
- no data-path claim is made from a memcpy-only fallback.

### P7 — RTC and low power

Scope:

- AON RTC and 32-kHz source;
- PM votes and sleep callback registration;
- DTIM/listen interval timing;
- RF off/on and PHY reinitialization;
- wake-source and low-voltage transitions.

This stage is deliberately last. The first Wi-Fi milestone keeps power save
off and proves RF initialization plus continuous scanning in the always-on
path first.

## 4. Merge protocol

For every stage:

1. Start from the latest `dev`.
2. Change only `chips/bk7258/`, board platform files, and required platform
   documentation/tests.
3. Build with Wi-Fi disabled.
4. Run the stage-specific positive and negative tests.
5. Review for Armino headers, Wi-Fi symbols, WPA, or `.a` dependencies.
6. Commit the stage as a focused change.
7. Merge the focused branch into `dev`.
8. Create the next platform worktree from the updated `dev`.
9. Rebase/sync `feature/bk7258-wifi` only after the platform stage is in `dev`.

The platform branch must never import the Wi-Fi branch to make a test pass.
The Wi-Fi branch may consume a platform API only after that API is in `dev`.

## 5. Wi-Fi handoff after platform stages

After P1–P6 are merged as needed, update the Wi-Fi branch:

- replace PMU/chipid stubs;
- replace analog loud-failure shims;
- replace OTP/eFuse stubs;
- replace flash/RF partition stubs;
- replace ADC/temp/voltage stubs;
- replace DMA/cache fallback;
- keep the vendor ABI tables sourced from the now-real platform APIs.

Then run the Wi-Fi milestones in order:

```text
vendor runtime init
  -> PHY/RF init
  -> 20 consecutive scans
  -> STA association/WPA
  -> NuttX RX/TX NetPKT bridge
  -> DHCP-done bridge
  -> static IP/ARP/ICMP
  -> DHCP/DNS/TCP/UDP
```

Do not claim Wi-Fi hardware success from the current link closure. The current
Wi-Fi link proves symbols and ABI reachability only; the platform stages prove
the hardware contracts that make those symbols meaningful.

## 6. Next action for the next session

Worktree:

```text
/home/czp/openvela_contest/.worktrees/bk7258-platform
branch: feature/bk7258-platform
base:  dev at 5c1e200
```

Start with P0:

1. inspect existing `bk7258_clock.c`, `bk7258_irq.c`, and `bk7258_gpio.c`;
2. compare them with the Armino BK7258 clock/ICU/GPIO implementation;
3. identify missing reusable platform APIs without touching `wifi/`;
4. add the smallest P0 implementation and its negative test;
5. build the normal CP configuration with Wi-Fi disabled;
6. commit and prepare the focused merge into `dev`.
