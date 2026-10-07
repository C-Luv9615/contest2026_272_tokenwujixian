# BK7258 芯片层与 Wi-Fi 适配目录

此目录承载所有与具体开发板无关的 BK7258 芯片层实现，并由仓库 manifest 映射到 `vendor/beken/chips/bk7258`。

## 当前形态

Wi-Fi 走**阶段一**（`docs/compose/spec/bk7258-wifi-native.md` 的 S2.2）：把 Armino 的编译单元原样编进 NuttX。

| 目录 | 内容 |
|---|---|
| `armino_baseline/` | T19 导入的 Armino 源码（基线 `d5b0278`，来源见其 `PROVENANCE.md`），不依赖外部 SDK |
| `wifi_armino/` | 阶段一的启动入口与接缝：FreeRTOS→NuttX、lwip→netdev、shell、`lwip_compat` |
| `wifi/hal_port/include/` | 芯片级 Armino 头兼容根（`common/`、`components/`、`os/`、`driver/`），全芯片共用 |
| `aon_pmu/ aon_rtc/ gpio/ pm/ saradc/ otp/ trng/ mb_ipc/` | 模块目录：`armino/` 权威拷贝 + `nuttx_port/` 接缝 |
| 顶层 `bk7258_*.c` | NuttX 风格芯片驱动（启动、时钟、中断、DMA、UART、PSRAM、sysctrl…） |
| `tools/` | 迁移期工具（编译清单、接口表、源码门禁）。阶段二结束后随门禁一并退役 |

构建配置用 `board/bk7258-devkit/configs/{cp-armino,ap-armino}`。

## 设计边界

CPU0 的启动、内存、链接与早期 UART 设计边界见 [BK7258 CPU0 Bring-up Contract](BK7258_CPU0_BRINGUP_CONTRACT.md)。
GDMA 适配记录见 [BK7258 GDMA Adaptation](BK7258_GDMA_ADAPTATION.md)。

## 历史文档

手写 Wi-Fi 适配线（`armino/` 源码拷贝 + `hal_port/` 胶水 + netdev 下半部，`CONFIG_BK7258_WIFI`）已于
2026-10-07 整体退役，其过程产物（ABI 审计、四次握手差异、能力表、启动回归报告、feature 计划）迁至
`/home/czp/openvela_contest/my_docs/bk7258-wifi/investigation/wifi-handwritten-line/`。
STA 适配方案与方法论在 `/home/czp/openvela_contest/my_docs/bk7258-wifi/`（总索引见该目录 `README.md`）。
