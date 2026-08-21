# BK7258 Platform Peripheral Matrix

## Purpose

This is the platform-side work breakdown for capabilities consumed by the
BK7258 Wi-Fi integration. It is intentionally independent of
`chips/bk7258/wifi/`: platform drivers must compile and be testable without
`CONFIG_BK7258_WIFI`, WPA, vendored Armino glue, or Beken `.a` files.

Armino is the hardware-contract source (register addresses, field values,
initialization order, units, and error behavior). The implementation target is
the OpenVela/NuttX chip or board driver framework, not an Armino runtime.

## Matrix

| Stage | Platform capability | Armino evidence to extract | OpenVela/NuttX target | Current state | Exit evidence |
| --- | --- | --- | --- | --- | --- |
| P0 | Clock/reset/IRQ/GPIO/pinmux | `middleware/soc/bk7258/`, `middleware/driver/sys_ctrl/`, `icu_map.h`, GPIO HAL | `chips/bk7258/bk7258_clock.c`, `bk7258_irq.c`, `bk7258_gpio.c`, `include/*` | Partial implementation exists | Register readback, IRQ attach/enable, GPIO mux readback; no Wi-Fi dependency |
| P1 | AON PMU + chip revision | `sys_ll.h`, `sys_pm_hal.c`, `aon_pmu_hal.h`, PMU register map | `bk7258_pmu.c`, `include/bk7258_pmu.h` | Not implemented | Chip ID matches known revision mask; PMU reads return defined errors on invalid access |
| P2 | Analog register transaction | `sys_set_ana_reg_bit`, `sys_ll_set_ana_reg*`, `sys_pm_hal.c` | `bk7258_analog.c`, `include/bk7258_analog.h` | Not implemented | Read/write/readback and negative invalid-mask test; no direct guessed `putreg32` |
| P3 | OTP/eFuse | `middleware/driver/otp/`, `efuse.h`, generated OTP map | `bk7258_otp.c`, `include/bk7258_otp.h` | Not implemented | Read-only MAC/trim reads; invalid item and write attempts fail safely |
| P4 | SARADC/temperature/voltage | `adc_driver.c`, `adc_hal.h`, `temp_detect.h`, units/calibration | `bk7258_adc.c`, `bk7258_temp.c`, `include/*` | Not implemented | Raw sample repeatability, units documented, timeout/error and calibration tests |
| P5 | Flash/MTD + RF calibration region | generated partition header, flash driver, packaging profile | `bk7258_flash.c` + board partition/profile | RF region absent from team profile | Erase/write/readback, bounds/blank-region behavior, power-loss-safe failure |
| P6 | DMA/cache | GDMA driver, descriptor alignment, cache maintenance sites | `bk7258_dma.c`, `bk7258_cache.c` or arch/chip hooks | Not implemented | Non-aligned and cache-enabled transfer tests; ownership/barrier documented |
| P7 | Low power/RTC | AON RTC, PM votes, LPO/DTIM timing | `bk7258_rtc.c`, PMU/clock APIs | Not implemented | Enter/exit timing, wake source, RF off/on and recovery; after RF scan works |

## Frozen order

1. P0 is the base and can be integrated independently of Wi-Fi.
2. P1–P4 establish chip identity and calibration inputs before RF bring-up.
3. P5 establishes the product-owned RF calibration storage contract.
4. P6 is required before trusting real MAC DMA TX/RX data.
5. P7 is deliberately last; first Wi-Fi milestone keeps power save off.

## Integration rule

After each platform stage is complete, build the normal BK7258 CP baseline
without Wi-Fi enabled. Only then should `feature/bk7258-wifi` consume the
platform API and replace its corresponding shim/stub. No Wi-Fi source or
prebuilt library is added to this platform branch.

## Known Wi-Fi blockers tracked outside this branch

- AON PMU/chip ID currently returns a placeholder in the Wi-Fi shim.
- Analog register writes are intentionally loud failures, never silent no-ops.
- The team board partition table has no RF calibration region yet.
- OTP ids currently came from an Armino generated product artifact and must be
  re-derived for the team product map.
- DMA/cache behavior is not proven by a host or link-only build.
