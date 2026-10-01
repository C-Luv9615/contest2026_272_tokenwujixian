---
feature: bk7258-wifi-native
status: designed
updated: 2026-10-01
branch: feat/bk7258-wifi-native
commits:
---

# BK7258 Vela 原生 Wi-Fi 适配

## Report

## [S1] Problem

BK7258 CP 上的 Wi-Fi 要在 NuttX 下工作，目标有两条：

1. 上层完全走 NuttX：netdev、NuttX 网络栈、wapi/ioctl。
2. 闭源库（`libwifi.a`、闭源 `libbk_phy.a`）依赖的硬件服务，最终全部由 NuttX 驱动提供。

闭源库原样复用。

以往做法的问题：先挑一部分 Armino 源码，再手写 shim 模拟其余部分，然后上板试错。每次语义对不上，都要上板一轮才能发现。本地 `dev`（`9550111`）比 `origin/dev-ai-contest-2026`（`a7de5d2`）多 81 个提交，用户确认 `ac1_tx_dma_dead` 就出现在这 81 个提交之后。

起点 `a7de5d2` 的现状：2026-10-01 在热点 `Luv` 上试了 3 次，只成功 1 次。

- 第 1 次失败：扫描发出后一直没有 `scanu_confirm`。
- 第 2 次失败：没有 `auth_send`，随后 `Authentication timed out`、`rw_msg_send timeout for 7172`，此后扫描结果一直为空。
- 第 3 次成功，见 `evidence-20261001/t1-a7de5d2-connect-ping.log`：
  - 第 554 行 `auth_send`，第 2161 行 `COMPLETED`；
  - 第 2838 行拿到地址 `10.153.235.167`；
  - 第 2926 行 ping 公网 10/10；
  - 全程 0 次 `tx_dma_dead`；
  - 第 2347、2663 行出现 `COMPLETED -> 4WAY_HANDSHAKE`，即连上之后又重新握手了 2 次。Armino iperf 参照日志（0904 镜像，10-01 板测）里这种重握手是 0 次。

## [S2] Design

### [S2.1] 参照基线

- 参照源码：Armino `~/armino/.worktrees/baseline-gcc13`，分支 `baseline/d2ded03-gcc13`，提交 `d5b0278`。它是 `d2ded03` 加 3 处 GCC 13.4 编译修复：cp/ap 的 `compile-options.cmake` 加 `-Wno-error` 和 `-includestdint.h`，`wpa_supplicant-2.10/src/utils/os_none.c` 加 `#include <time.h>`。
- 参照工程：`make bk7258 PROJECT=wifi/iperf`。
- 参照证据：2026-10-01 板测，PM 和 STA_PS 开启，连接、DHCP 正常，`ping 223.5.5.5` 4/4。
- 闭源库身份：`libwifi.a` sha256 前缀 `6e7e018c`，闭源 `libbk_phy.a` 前缀 `fb3e8f6a`，与本仓 prebuilt 一致。
- 规格输入：该构建产出的 `build/bk7258/iperf/bk7258/{compile_commands.json,app.map,config/sdkconfig.h}`。CP 共 539 个编译单元。

### [S2.2] 两阶段

阶段一：先在 NuttX 上原样跑通。

- 编译单元集合 = 基线 CP `compile_commands.json` 的 539 个单元，减去按 S2.7 裁决出来的排除集。
- 每个保留单元都用基线原件，以及原来的 `-D`、`-I`、`-include` 和编译选项。CMake 源码列表和逐文件编译参数由脚本从 `compile_commands.json` 生成，不手写。
- 配置使用基线生成的 `sdkconfig.h`。
- 手写代码只允许出现在 S2.7 替换表登记过的位置。

阶段二：硬件服务原生化。

- 按 S2.5 的范围，每次只把一个硬件目标文件换成 NuttX 原生驱动。
- 每次替换后，都用 T5 的轨迹和 Armino 参照对比，并上板验收。
- 某一步失败，问题只可能出在这一次替换上。

### [S2.3] 源码一致性规则

阶段一里每个保留的 Armino 源文件，都必须和基线 `d5b0278` 的同路径文件逐字节一致，由比对脚本做门禁，例外清单为空。

`a7de5d2` 的 `chips/bk7258/wifi/armino/` 共 416 个文件：383 个一致，32 个改过，1 个在基线里没有对应文件（`glue/bk_wifi/src_rwnx_td.c`）。阶段一不沿用这些改过的文件，直接取基线原文件。

### [S2.4] 加密

阶段一和基线保持一致：wpa 使用 `crypto_mbedtls.c`，配合 Armino `psa_mbedtls` 3.5.2，二者都在基线编译单元里。

这条会替换 2026-10-01 早先定下的"先用 wpa 内部加密"：那是"挑一部分源码"做法下的选择，和 S2.2 的原样原则冲突。

迁移到 NuttX mbedtls 3.4.0 见 S3。

### [S2.5] 硬件服务契约（阶段二）

范围的确定方法：从基线 `app.map` 的 Cross Reference 表出发，由闭源库开始，经过开源逻辑层往下追。

结果是 79 个硬件目标文件，其中 27 个被 Wi-Fi 侧直接引用：

- `pm`、`sys_pm_hal`
- `adc_driver`、`aon_rtc_driver_64bit`、`ckmn_driver`、`dma_driver`
- `flash_driver`、`flash_driver_ext`、`flash_partition`
- `gpio_driver`、`gpio_driver_base`
- `interrupt_base`、`mailbox_channel`、`mb_ipc`、`mb_ipc_heartbeat`
- `otp_driver_v1_1`、`printf`、`reset_reason`、`sdmadc_driver`
- `sys_int_driver`、`sys_ps_driver`、`sys_wifi_driver`
- `timer_driver`、`trng_driver`、`uart_driver`、`wdt_driver`、`trap_base`

阶段二必须把这 27 个全部原生化。另外 52 个只有在被保留代码实际引用时才纳入。

每个目标文件替换时：

- 要提供的符号集合以 T2 生成的表为准；
- 语义以基线源码为准；
- 验证方法见 S2.6。

### [S2.6] 验证模型

- 每个改变运行行为的任务都要上板：连接 `Luv`，确认 4 次握手完成、DHCP 拿到地址，再 ping 一个公网 IP 4/4。
- 阶段一完成（T11）之前：每次上板允许复位后最多试 3 次，成功 1 次就算通过，但每次失败的方式都要记录。
- T11 本身，以及阶段二的每一步：连接必须 3/3 成功。
- 每份证据要记录：固件包 sha256、烧录命令、UART 日志，以及源码 mtime 早于打包时间的证明（见 AGENTS.md）。
- 日志里的 PSK 替换为 `<PSK-REDACTED>`。固件自己打印的 `password=...` 也要替换。`WPA: TK` 那一行删除。
- 所有结论都必须指向具体的命令输出或日志行号。没有证据的说法写成任务，不能写成结论。

### [S2.7] 排除规则

一个组件可以排除，必须满足：它导出、并且被保留代码用到的每个符号，都在替换表里有一行，写明：

- NuttX 侧由谁提供；
- 语义依据，即基线源码的 `file:line`。

有任何一个符号写不出这两项，这个组件就保留原文件。已有成果（`hal_port/`、`a7de5d2` 的 shim 等）只能作为提供者的候选，必须按这张表重新核对，不能直接认定可用。

候选组件及证据如下。数据来自 `d5b0278` 的 `app.map`。"被用到的导出符号"指组件之外有引用的符号数；调用方后面的数字，是该调用方用到其中多少个，只列前 6 名。

| 组件 | 编译单元 | 被用到的导出符号 | 调用方 |
|---|---|---|---|
| `os_source`（FreeRTOS 内核） | 11 | 81 | bk_rtos 71、bk_system 4、psa_mbedtls 4、cmsis 3、lwip 3、coredump 3 |
| `bk_rtos` | 7 | 102 | bk_wifi 60、wpa 45、driver 42、lwip 39、bk_cli 37、预编译库 27 |
| `lwip_intf_v2_1` | 52 | 46 | bk_wifi 27、iperf 19、controller_if 9、bk_netif 9、wpa 4、bk_cli 3 |
| `cm33` | 10 | 28 | driver 10、bk7258 5、bk_cli 3、bk_rtos 2、cmsis 2、os_source 2 |
| `cmsis` | 2 | 1 | common 1 |
| `bk_startup` | 2 | 8 | bk_cli 4、driver 4、bk_pm 2、cm33 2、cmsis 1、cp 1 |

`bk_cli`、`at_server`、`unity`、iperf demo 这几个组件，只有链接证明没有保留代码引用它们时，才能排除。

## [S3] Out of Scope

- 把 wpa 加密迁移到 NuttX mbedtls：另起 feature。
- AP 核侧改动、SoftAP、BLE、WPA3/SAE 验证。
- 整体合入 `dev` 那 81 个提交。
- 推送分支、发 PR：由用户另行决定。

## Tasks

- [x] T1: 复现起点。acceptance：构建通过；连接成功后 ping 公网 4/4；记录 `tx_dma_dead` 的次数和行号。结果：构建、decode、完整性门禁都通过；镜像 sha256 `121d3cc7…1af7`；`Luv` 上连接 1/3；ping 10/10；`tx_dma_dead` 0 次。证据见 S1。(covers: S2.6)
- [ ] T2: 接口表提取脚本。读取基线 `app.map`、`compile_commands.json`，对闭源库跑 `nm`，输出"符号 → 提供者目标文件 → 源文件 → 组件"的对照表。脚本和生成结果一起入库。acceptance：重复运行输出一致；闭源库 167 个外部符号中 164 个有提供者，其余 3 个（`malloc`、`free`、`dbg_env`）在表中写明来源；S2.7 表中的数字能从脚本输出复现。(covers: S2.1, S2.5, S2.7)
- [ ] T3: 源码一致性门禁脚本。拿阶段一的每个保留源文件和基线逐字节比对，出现例外清单之外的差异时返回非零。acceptance：对 `a7de5d2` 现有的 `armino/` 运行，能准确报出 32 个改动文件和 1 个无对应文件，并返回非零。(covers: S2.3)
- [ ] T4: 编译清单生成脚本。从基线 `compile_commands.json` 生成 CMake 源码列表和逐文件编译参数。acceptance：重复生成结果一致；每个单元的参数和基线相同，只有登记过理由的工具链等价替换可以不同。(covers: S2.2; depends: T2)
- [ ] T5: 双端轨迹工具。在我们这边，用 NSH 命令抓 MAC、TX DMA、PMU 寄存器快照，并记录 `g_wifi_funcs` 调用；在 Armino 那边，在独立的 debug 分支（不是 `baseline/d2ded03-gcc13`）上加一条输出格式相同的 CLI 命令。轨迹里要包含重握手次数。acceptance：两端都在连接完成后抓一次，输出可以直接 diff。(covers: S2.5)
- [ ] T6: 参照负载基线。Armino 参照固件跑 30 分钟 TX 负载：优先用 `iperf -c`；热点有客户端隔离时，改用最大包长的连续 ping 公网。acceptance：统计 `tx_dma_dead`、assert、重启的次数，并记录用了哪种负载方式。(covers: S2.6)
- [ ] T7: 排除裁决和替换表。对 S2.7 的 6 个候选组件，以及 4 个可选组件，逐个符号写出 NuttX 提供者和语义依据。写不出的组件改为保留。acceptance：表里覆盖了 T2 输出中这些组件的全部被用符号，没有写"待定"的行；每个组件的最终状态（排除或保留）都有依据。(covers: S2.7; depends: T2)
- [ ] T8: 阶段一构建。按 T4 的清单和 T7 的裁决编进 CP。acceptance：链接没有 undefined 符号；重复定义只出现在替换表登记过的位置；L1 校验、decode、完整性门禁都通过；T3 门禁返回 0。(covers: S2.2, S2.3, S2.4; depends: T3, T4, T9, T10)
- [ ] T9: OS 接缝。实现 T7 替换表里 `bk_rtos` 和 `os_source` 那部分由 NuttX 提供的符号。acceptance：每个符号恰好定义一次；主机测试覆盖队列 item_size、栈按字计、临界区嵌套、ISR 上下文发送四项。(covers: S2.7; depends: T7)
- [ ] T10: netdev 胶水。实现 T7 替换表里 `lwip_intf_v2_1` 那部分的符号，接到 NuttX netdev。acceptance：链接 map 里没有 lwip 目标文件；`wapi` 连接后 `renew wlan0` 能拿到地址。(covers: S2.2, S2.7; depends: T7)
- [ ] T11: 阶段一上板验收。acceptance：`Luv` 上连接 3/3；ping 公网 4/4；重握手次数、`tx_dma_dead` 次数不多于 Armino 参照。(covers: S2.2, S2.6; depends: T8)
- [ ] T12: 阶段二：PM 和电源类原生化，范围为 `pm`、`sys_pm_hal`、`sys_wifi_driver`、`sys_ps_driver`、`sys_int_driver`。acceptance：链接 map 证明这些目标文件不再来自 Armino 源文件；T5 轨迹与参照一致；连接 3/3；ping 4/4。(covers: S2.5; depends: T5, T11)
- [ ] T13: 阶段二：DMA、时钟、定时器类，范围为 `dma_driver`、`ckmn_driver`、`timer_driver`、`aon_rtc_driver_64bit`。acceptance 同 T12。(covers: S2.5; depends: T12)
- [ ] T14: 阶段二：Flash，范围为 `flash_driver`、`flash_driver_ext`、`flash_partition`，改为基于 NuttX MTD。acceptance 同 T12，另外 easyflash 写入的数据重启后仍在。(covers: S2.5; depends: T11)
- [ ] T15: 阶段二：校准类，范围为 `adc_driver`、`sdmadc_driver`、`otp_driver_v1_1`、`trng_driver`。acceptance 同 T12，另外校准日志里的 `xtal`、`pwr_gain` 与参照一致。(covers: S2.5; depends: T11)
- [ ] T16: 阶段二：mailbox 类，范围为 `mailbox_channel`、`mb_ipc`、`mb_ipc_heartbeat`，与 `bk7258-armino-otp-mb-ipc.md` 对齐。acceptance 同 T12。(covers: S2.5; depends: T11)
- [ ] T17: 阶段二：杂项，范围为 `interrupt_base`、`trap_base`、`printf`、`uart_driver`、`wdt_driver`、`reset_reason`、`gpio_driver`、`gpio_driver_base`。acceptance 同 T12。(covers: S2.5; depends: T11)
- [ ] T18: 最终验收。按 T6 的同一种负载跑 30 分钟。acceptance：`tx_dma_dead`、assert、重启的次数不多于 T6 中 Armino 的结果；ping 20/20；证据按 S2.6 齐全。(covers: S2.6; depends: T6, T12, T13, T14, T15, T16, T17)
