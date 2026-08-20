# Beken patch manifest (P0 artifact)

Team-owned delivery contract for the BK7258 Wi-Fi vendor integration. The
team repository does **not** copy Armino/Beken source; every external input is
pinned here with a hash and a legal note so the integration is reproducible
and reviewable.

## 1. Upstream

| Field | Value |
| --- | --- |
| SDK | Armino `bk_avdk_smp` |
| Branch | `release/v3.1.1` |
| Commit | `d2ded037798530175e5dc5cde6fa1878f5d5ef35` |
| App project | `bk_solution_ai` `beken_genie` (lock file `beken_genie` revision `aa370357ea31567ab43091513145f91fd38efd9e`) |

## 2. Binary inventory (SHA-256)

| Library | SHA-256 |
| --- | --- |
| `libwifi.a` | `6e7e018c884674ced3305d241138eae004c4c3d8568123573c24ec9c71d37b0c` |
| `libbk_phy.a` | `fb3e8f6a6782f6fab6edde59ec95417c0da1da2ab8263230848535b34e4a6979` |
| `libcom_phy.a` | `208aa99e8b794d587c8161a7b861efebbe2c47789f85e3c025189d712455e0c1` |
| `libwifi_nspe.a` | `564f563984cf4d3bad3b101283420c470a665b2bed3dbd6b40bf767558b1e94a` |
| `libbk_phy_nspe.a` | `17bd0d0e81a1f01aed634e26283b3c502466586e319ff13c1a62fea302c695cf` |

## 3. Integration patches (filled as the integration branch lands)

| Patch | Purpose | SHA-256 |
| --- | --- | --- |
| `beken/nuttx-netdev.patch` | add `CONFIG_BK_WIFI_NUTTX_NETDEV` / vendor-packet split | *(pending)* |
| `beken/osal-adapter.patch` | map `wifi_os_funcs_t` onto `bk7258_wifi_osal.c` | *(pending)* |
| `beken/eapol-rx-bridge.patch` | divert EAPOL/WAI to vendor WPA, data to lower half | *(pending)* |

## 4. Configuration hash

| Item | SHA-256 |
| --- | --- |
| `beken_genie/cp/config/bk7258/config` | *(record at integration)* |
| profile reserve (`108` head + `600` desc) | *(pin to library hash)* |

## 5. License

Armino root carries Apache-2.0 (`LICENSE`). Binary relink, redistribution, and
NuttX-buildable delivery must be confirmed with Beken before release. Team
code in this repository remains team-owned and independently licensed.

## 6. Delivery boundary

- Team repo: NuttX lower-half, OSAL, packet shim, hw/board glue, Kconfig,
  build integration — all under `chips/bk7258/wifi/` and
  `board/bk7258-devkit/src/bk7258_wifi_board.c`.
- Beken/Armino repo or a separate PR: any modification to `rwnx_*`, `rw_task`,
  `wifi_v2`, `bk_wifi_adapter` or the WPA/EAPOL path.
