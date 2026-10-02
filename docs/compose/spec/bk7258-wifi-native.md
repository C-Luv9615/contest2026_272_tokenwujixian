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

- 编译单元集合 = T4 清单的 538 个单元，减去 S2.7 排除记录里的单元。默认全部保留。
- 每个保留单元都用基线原件，以及原来的 `-D`、`-I`、`-include` 和编译选项。CMake 源码列表和逐文件编译参数由脚本从 `compile_commands.json` 生成，不手写。
- 配置使用基线生成的 `sdkconfig.h`。
- 手写代码只允许出现在三处：FreeRTOS API 到 NuttX 的接缝（T9）；netdev 胶水（T10）；启动入口 `chips/bk7258/wifi_armino/bk7258_armino_start.c`，它在 NuttX 内核线程里代替基线的 `Reset_Handler → entry_main → 调度器 → main()` 这条链（这条链和 NuttX 启动冲突），调用基线 `projects/wifi/iperf/cp/cp_main.c` 里的入口。
- 每个组件建成一个静态库（`nuttx_add_kernel_library`），进入 NuttX 链接时的 `--start-group`，和基线一样只拉入被引用的成员。不直接编进 `arch`，否则所有成员都会被强制链入。
- libc 头用 NuttX 的：NuttX 给每个库加的 `-isystem nuttx/include` 会让 Armino 单元按 NuttX 的结构布局编译，最终链接的也是 NuttX libc。这是对"原参数"的第一处有意偏离。
- 第二处偏离：保留组件参数里的 lwip 裸头目录 `lwip-2.1.2/src/include/lwip` 被原位替换为 `chips/bk7258/wifi_armino/lwip_compat`。原因：lwip 已排除，而这个目录里的 `errno.h`、`netdb.h`（通用 POSIX 头，与 NuttX 重复；`errno.h` 的 122 个 `E*` 数值与 NuttX 完全一致）以及 `debug.h`、`err.h`（同名不同义）会遮蔽 NuttX 自己的头，连 NuttX 内部的 `#include <errno.h>` 也被劫持。通用头只用 NuttX 的一份；`lwip_compat` 只转发基线依赖记录证明解析到这里的 lwip 独有头（`pbuf.h`、`netif.h`），属于 T10 的范围。
- 源码位置：阶段一通过一个必填的 CMake 变量 `BK7258_ARMINO_BASELINE_DIR` 指向本机 Armino 基线，变量没设置就报错，不在构建文件里写死本机路径。跑通后按 AGENTS.md 把用到的文件导入 `chips/bk7258/` 并保留来源（T19）。
- 阶段一放在一个新的 Kconfig 开关后面，开关关闭时现有 Wi-Fi 构建不受影响。

阶段二：硬件服务原生化。

- 按 S2.5 的范围，每次只把一个硬件目标文件换成 NuttX 原生驱动。
- 每次替换后，都用 T5 的日志对比和 Armino 参照比较，并上板验收。
- 某一步失败，问题只可能出在这一次替换上。

### [S2.3] 源码一致性规则

阶段一里每个保留的 Armino 源文件，都必须和基线 `d5b0278` 的同路径文件逐字节一致，由比对脚本做门禁，例外清单为空。

`a7de5d2` 的 `chips/bk7258/wifi/armino/` 共 750 个源文件和头文件（T3 统计）：704 个一致，43 个改过（`.c` 32、`.h` 11），3 个在基线里没有对应文件（`.c` 1、`.h` 2）。阶段一不沿用这些改过的文件，直接取基线原文件。

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

默认保留。一个编译单元只有在拿到下面两种证据之一时才排除，证据写进 T7 的排除记录：

1. 链接器报重复定义：贴出链接输出里的那一行，以及 NuttX 侧的定义位置。
2. 同一个资源有两个主人，例如两套调度器、两个复位入口、两张中断向量表：写明资源名称，以及双方占用它的代码位置。

目前已定的排除只有两项：

- `os_source`（FreeRTOS 内核 11 个单元）：调度器只能有一个，用 NuttX 的。`bk_rtos` 原样保留，它调用的 FreeRTOS API 由 T9 的接缝在 NuttX 上实现。
- `lwip_intf_v2_1`（52 个单元）：用户决定第一阶段就使用 NuttX 自己的 TCP/IP 协议栈（NuttX 不带 lwip，`nuttx/net/` 是它自己的实现）。lwip 被用到的符号由 T10 的 netdev 胶水提供。

原则：OS 和 POSIX 接口（libc、signal、pthread、socket、spinlock）一律用 NuttX 的。Armino 自带的同名副本排除或屏蔽，由 `chips/bk7258/wifi_armino/compat` 对接，不改 Armino 源码。lwip 协议栈内部接口（pbuf、netif、memp 等）不是 OS 接口，归 T10。

按这条原则已处理（证据来自 T8 第二次构建和基线依赖记录）：

- `bk_rtos/.../pthread_local_storage.c` 排除：它定义自己的 `pthread_key_t`，和 NuttX 冲突；导出符号只被已排除的 lwip 和 os_source 调用。
- `wpa_supplicant-2.10/bk_patch/signal.h` 屏蔽：它是无 OS 环境下的 `<signal.h>` 替身，遮蔽了 NuttX 的版本（103 个单元因此报 `sigset_t` 未定义）。保留代码用到的 `SIGALRM`、`SIGHUP`、`SIGINT`、`SIGTERM` 两边数值一致；唯一不同的 `SIGUSR1` 只在未编译的 `hostapd/main.c` 里。`bk_patch/signal.c` 只有空函数，排除后由 compat 头里的 static inline 提供。
- `bk_cli/shell_uart.c`、`bk_cli/cli_rpc.c` 排除，由 `compat/bk7258_armino_shell.c` 接到 NuttX：Armino 的 CLI 和日志（`cli_main.c`、`shell_task.c`）作为应用层原样保留。`shell_uart.c` 自己初始化 UART0 并注册中断，而 UART0 是 NuttX 控制台，两个主人。新设备保持同样的 8 格队列和完成回调顺序，输出走 `up_putc`，输入归 NSH。`cli_rpc.c` 对外只导出 `cli_debug_cmd_table`（`cli_main.c:961` 调用），由空表替代。
- lwip 的 `sockets.h`、`inet.h` 用 `wifi_armino/lwip_os` 中的替换头：socket 是 OS 接口，lwip 已排除。原头文件和 NuttX 重复定义了 `socklen_t`、`sockaddr`、`iovec`、`in_addr` 等，还用宏把 `socket()` 等改名成 `lwip_*()`。这份 lwip 没有 `LWIP_SOCKET_EXTERNAL_HEADERS` 开关，所以用替换头，并沿用 lwip 原来的头文件保护宏。保留代码直接调用 BSD 名字，现在走到 NuttX；`closesocket`、`ioctlsocket` 映射到 `close`、`ioctl`。`lwip/stats.h` 原样使用，只在它周围暂停 NuttX 的 `opterr` 宏（字段名冲突；保留代码不读这个字段）。
- 中断：NVIC 归 NuttX。排除 `icu/interrupt_base.c`、`arch/cm33/arch_interrupt.c`、`driver/bk7258/interrupt.c`，由 `compat/bk7258_armino_irq.c` 提供 `bk_int_*`：源 n 接到 NuttX IRQ `NVIC_IRQ_FIRST + n`（`icu_map.h` 64 行全部是同号映射，用基线调试信息逐行核对），优先级照基线（`INT_SRC_LCD` 为 0，其余为 6，3 位优先级）。注册只开 NVIC，SYS 中断使能仍由各 Armino 驱动自己开。`interrupt_init` 为空：原版会关掉 PRIMASK 和全部 64 路 NVIC。若某个 IRQ 已被 NuttX 驱动打开，拒绝注册并打日志。为此 `cp-armino` 关掉 NuttX 的 GDMA 和 TIMER0 驱动，这两个源交给 Armino。
- `bk_wifi/include/wifi_spinlock.h` 屏蔽：它用同目录引号包含，包含路径挡不住，所以用 `-include` 预先定义它的头文件保护宏。`spinlock_t` 用 NuttX 的类型；锁操作照基线保持空操作，对应的 NuttX 调用写在注释里。

其余组件，包括 `cm33`、`cmsis`、`bk_startup`、`bk_cli`、`at_server`、`unity`、iperf demo，一律以 T8 链接报出的冲突为准，不预先排除。已有成果（`hal_port/`、`a7de5d2` 的 shim 等）只能作为 T9、T10 实现时的参考，不能直接拿来用。

下表是候选组件对外被用到的情况，用来估计 T9、T10 的工作量。数据来自 `d5b0278` 的 `app.map`。"被用到的导出符号"指组件之外有引用的符号数；调用方后面的数字，是该调用方用到其中多少个，只列前 6 名。

| 组件 | 编译单元 | 被用到的导出符号 | 调用方 |
|---|---|---|---|
| `os_source`（FreeRTOS 内核） | 11 | 81 | bk_rtos 71、bk_system 4、psa_mbedtls 4、cmsis 3、lwip 3、coredump 3 |
| `bk_rtos` | 7 | 102 | bk_wifi 60、wpa 45、driver 42、lwip 39、bk_cli 37、bk_phy 24；另有 22 个被闭源库直接引用（libwifi 20、libbk_phy 5） |
| `lwip_intf_v2_1` | 52 | 46 | bk_wifi 27、iperf 19、controller_if 9、bk_netif 9、wpa 4、bk_cli 3 |
| `cm33` | 10 | 28 | driver 10、bk7258 5、bk_cli 3、bk_rtos 2、cmsis 2、os_source 2 |
| `cmsis` | 2 | 1 | common 1 |
| `bk_startup` | 2 | 8 | bk_cli 4、driver 4、bk_pm 2、cm33 2、bk_init 1、cmsis 1、cp 1 |

### [S2.8] 日志对比

不抓寄存器、不插桩。第一阶段编进来的是 Armino 原文件，它们自带的日志（`wifid`、`wpa`、`rwnx`、`hitf`、`lwip` 等前缀）在我们的固件里会原样打印，所以直接比较两份 UART 日志里关键事件的顺序和次数：scan 开始与完成、auth、assoc、4 次握手各状态、DHCP、重握手次数、`tx_dma_dead`、assert、重启。Armino 参照固件不需要另开 debug 分支。

## [S3] Out of Scope

- 把 wpa 加密迁移到 NuttX mbedtls：另起 feature。
- AP 核侧改动、SoftAP、BLE、WPA3/SAE 验证。
- 整体合入 `dev` 那 81 个提交。
- 推送分支、发 PR：由用户另行决定。

## Tasks

- [x] T1: 复现起点。acceptance：构建通过；连接成功后 ping 公网 4/4；记录 `tx_dma_dead` 的次数和行号。结果：构建、decode、完整性门禁都通过；镜像 sha256 `121d3cc7…1af7`；`Luv` 上连接 1/3；ping 10/10；`tx_dma_dead` 0 次。证据见 S1。(covers: S2.6)
- [x] T2: 接口表提取脚本。结果：`chips/bk7258/tools/armino_interface.py` 生成 `chips/bk7258/tools/armino_baseline/{interface.tsv,summary.txt}`，`--check` 返回 0；167 个外部符号中 164 个有提供者；`malloc`/`free` 经 `--wrap` 由 `bk_rtos/platform_stub.c` 提供；`dbg_env` 只被 `dbg_task.c.obj` 引用，该成员没有链入；硬件目标文件 79 个，直接 27 个。读取基线 `app.map`、`compile_commands.json`，对闭源库跑 `nm`，输出"符号 → 提供者目标文件 → 源文件 → 组件"的对照表。脚本和生成结果一起入库。acceptance：重复运行输出一致；闭源库 167 个外部符号中 164 个有提供者，其余 3 个（`malloc`、`free`、`dbg_env`）在表中写明来源；S2.7 表中的数字能从脚本输出复现。(covers: S2.1, S2.5, S2.7)
- [x] T3: 源码一致性门禁脚本。结果：`chips/bk7258/tools/armino_source_gate.py` 按目录前缀把文件映射到基线（`glue/`、`wpa_supplicant/`、`board/`、`include/common/`、`include/`），比对 `.c`/`.h`/`.S`。在 `a7de5d2` 上：`.c` 改动 32 个、无对应 1 个（`glue/bk_wifi/src_rwnx_td.c`，没有任何构建文件引用它），返回 1。另外查出头文件改动 11 个、无对应 2 个（`vnd_cal/include/bk_arm_arch.h`、`bk_misc.h` 是手写的空兼容头，基线对应文件分别在 `cp/middleware/driver/include/bk_private/` 和 `cp/components/bk_common/include/`）。自测：逐字节相同返回 0，改 1 个字节返回 1，多出无对应文件返回 1，全部列入例外后返回 0。拿阶段一的每个保留源文件和基线逐字节比对，出现例外清单之外的差异时返回非零。acceptance：对 `a7de5d2` 现有的 `armino/` 运行，能准确报出 32 个改动文件和 1 个无对应文件，并返回非零。(covers: S2.3)
- [x] T4: 编译清单生成脚本。结果：`chips/bk7258/tools/armino_compile_manifest.py` 生成 `chips/bk7258/tools/armino_baseline/{armino_units.tsv,component_flags.tsv}`。538 个编译单元、38 个组件；同一组件内所有单元的编译参数一致（去掉 `-o`、`-MF`/`-MT`、`-D__FILE__` 和源文件位置这些逐文件差异后），因此按组件存一份参数就够。`-I` 路径把基线树前缀重写成 `@ARMINO@`，消费方再替换成本机路径。参数按 shell 规则（shlex）解析，每个 argv 元素占一列，所以 `"-Wl, --gc-sections"`、`-DAPP_VERSION="unknown"` 这类带空格或引号的参数保持完整。`--check` 返回 0；全部 538 个单元逐个和 `compile_commands.json` 原始 argv 比对，0 处不一致。从基线 `compile_commands.json` 生成 CMake 源码列表和逐文件编译参数。acceptance：重复生成结果一致；每个单元的参数和基线相同，只有登记过理由的工具链等价替换可以不同。(covers: S2.2; depends: T2)
- [ ] T5: 日志对比脚本。输入两份 UART 日志（Armino 参照、我们的固件），按 S2.8 抽取关键事件序列和计数，输出两边的差异。acceptance：对 `/home/czp/iperf-ref-1001.log` 和 `evidence-20261001/t1-a7de5d2-connect-ping.log` 运行，能报出 T1 的 2 次重握手，而参照为 0 次。(covers: S2.8)
- [ ] T6: 参照负载基线。Armino 参照固件跑 30 分钟 TX 负载：优先用 `iperf -c`；热点有客户端隔离时，改用最大包长的连续 ping 公网。acceptance：统计 `tx_dma_dead`、assert、重启的次数，并记录用了哪种负载方式。(covers: S2.6)
- [ ] T7: 排除记录。根据 T8 的链接输出和资源冲突，为每个被排除的单元下 S2.7 要求的证据。acceptance：每个排除单元都能对应到一行链接输出，或一条写明双方代码位置的资源冲突说明；`os_source`、`lwip_intf_v2_1` 两项的依据已写入。(covers: S2.7; depends: T8)
- [x] T8: 阶段一 CMake 接入和首次链接。结果：开关 `BK7258_WIFI_ARMINO_BASELINE`、`chips/bk7258/wifi_armino/`、`configs/cp-armino`；保留单元全部编译通过（第 6 次 keep-going 构建只剩链接失败）。首次链接清单在 `chips/bk7258/tools/armino_baseline/phase1_link.txt`：93 个 undefined（os_source 48、lwip 31、链接脚本符号 10、其他 4）、6 个重复定义（`bk_mailbox_*`）、flash 超出 111660 字节。开关关闭时 `cp` 的 `app.bin` 仍是 `1c8011f5`；`nuttx.elf` 去掉调试段后与改动前逐字节一致（`425b6535`），差异只在 `bk7258_bringup.c` 的行号表。新的 Kconfig 开关打开时，CP 的 CMake 读取 T4 清单，按组件参数编译保留单元，路径来自 `BK7258_ARMINO_BASELINE_DIR`，配置用基线 `sdkconfig.h`。acceptance：开关关闭时，`a7de5d2` 的构建结果不变；开关打开时，所有保留单元都能编过；链接输出里的 undefined 和重复定义符号整理成清单入库。(covers: S2.2, S2.4; depends: T3, T4)
- [ ] T9: OS 接缝。在 NuttX 上实现 T8 清单里 FreeRTOS API 那部分 undefined 符号，供原样保留的 `bk_rtos` 调用。sdk-source 分支修过的 9 个问题（队列 item_size、栈按字计、临界区嵌套、ISR 上下文判断、`nxmq_*` 无取消点等）要带过来。acceptance：这些符号各定义一次；主机测试覆盖队列 item_size、栈按字计、临界区嵌套、ISR 上下文发送四项。(covers: S2.2, S2.7; depends: T7, T8)
- [ ] T10: netdev 胶水。实现 T8 清单里原本由 lwip 提供的符号，接到 NuttX netdev。acceptance：链接 map 里没有 lwip 目标文件；`wapi` 连接后 `renew wlan0` 能拿到地址。(covers: S2.2, S2.7; depends: T7, T8)
- [ ] T11: 阶段一链接和上板验收。acceptance：链接没有 undefined 符号；重复定义只出现在 T7 记录过的位置；L1 校验、decode、完整性门禁都通过；`Luv` 上连接 3/3；ping 公网 4/4；T5 对比显示重握手次数、`tx_dma_dead` 次数不多于 Armino 参照。(covers: S2.2, S2.6; depends: T5, T9, T10)
- [ ] T12: 阶段二：PM 和电源类原生化，范围为 `pm`、`sys_pm_hal`、`sys_wifi_driver`、`sys_ps_driver`、`sys_int_driver`。acceptance：链接 map 证明这些目标文件不再来自 Armino 源文件；T5 对比没有新增差异；连接 3/3；ping 4/4。(covers: S2.5; depends: T11)
- [ ] T13: 阶段二：DMA、时钟、定时器类，范围为 `dma_driver`、`ckmn_driver`、`timer_driver`、`aon_rtc_driver_64bit`。acceptance 同 T12。(covers: S2.5; depends: T12)
- [ ] T14: 阶段二：Flash，范围为 `flash_driver`、`flash_driver_ext`、`flash_partition`，改为基于 NuttX MTD。acceptance 同 T12，另外 easyflash 写入的数据重启后仍在。(covers: S2.5; depends: T11)
- [ ] T15: 阶段二：校准类，范围为 `adc_driver`、`sdmadc_driver`、`otp_driver_v1_1`、`trng_driver`。acceptance 同 T12，另外校准日志里的 `xtal`、`pwr_gain` 与参照一致。(covers: S2.5; depends: T11)
- [ ] T16: 阶段二：mailbox 类，范围为 `mailbox_channel`、`mb_ipc`、`mb_ipc_heartbeat`，与 `bk7258-armino-otp-mb-ipc.md` 对齐。acceptance 同 T12。(covers: S2.5; depends: T11)
- [ ] T17: 阶段二：杂项，范围为 `interrupt_base`、`trap_base`、`printf`、`uart_driver`、`wdt_driver`、`reset_reason`、`gpio_driver`、`gpio_driver_base`。acceptance 同 T12。(covers: S2.5; depends: T11)
- [ ] T18: 最终验收。按 T6 的同一种负载跑 30 分钟。acceptance：`tx_dma_dead`、assert、重启的次数不多于 T6 中 Armino 的结果；ping 20/20；证据按 S2.6 齐全。(covers: S2.6; depends: T6, T12, T13, T14, T15, T16, T17)
- [ ] T19: 导入仓库。把阶段一仍在使用的 Armino 原文件按 AGENTS.md 导入 `chips/bk7258/`，保留模块结构和来源记录，去掉对 `BK7258_ARMINO_BASELINE_DIR` 的依赖。acceptance：干净检出、不设该变量也能构建；T3 门禁对导入目录返回 0。(covers: S2.2, S2.3; depends: T11)
