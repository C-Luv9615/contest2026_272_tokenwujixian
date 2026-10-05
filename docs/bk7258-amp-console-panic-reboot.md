# BK7258 双核 console、panic 现场与重启设计

CP、AP 各跑一份 NuttX（AMP），CP 管 Wi‑Fi、物理串口和 CPU1 的启停，AP 跑应用。这份记录三件事怎么定下来的：交互终端放在哪个核、崩溃时怎么拿到两个核的现场、重启由谁执行。

## 现在到哪了

全部在板上验证过：

| commit | 内容 | 板上结果 |
|---|---|---|
| `bdf3d28` | AP panic 时把崩溃位置写进 SWAP，由 CP 打印（方案 A） | 作为兜底保留 |
| `60776fc` | UART0 留在 CP，开机自动 `cu` 到 AP 的 NSH | 自动进入 `nsh-ap>`，`~.` 回 CP，`cu` 再回 AP |
| `10157a9` | 任一核 panic，两个核的现场都打到 UART0 | AP 先崩、CP 先崩都完整，无 BusFault，打完停住 |
| `29908c4` | 两个核回到 EHABI unwinder | UsageFault 的 PC 定位到 `bk7258_panic_test.c:23`，没有假调用 |
| `b872b06` | 任一核 `reboot` 都由 CP 做整片复位 | AP `reboot` 4/4、CP `reboot`、`nsh-ap>` 下免按键烧录都通过 |
| `8862f04` | `BOARD_RESET_ON_ASSERT>=1` 时 panic 后自动重启 | AP 先崩、CP 先崩都能自动回到 `nsh-ap>`；默认仍为 0 |

测试命令：`bk7258_panic` 以中断级调用 `_assert`，`bk7258_panic fault` 执行一条非法指令，走真实的 UsageFault。

## 一、交互终端放在哪个核

### 结论：UART0 留在 CP，开机自动接到 AP

CP 的启动入口改为 `bk7258_cp_init_main`（`board/.../src/bk7258_console.c`）。流程：

1. 先做 NSH 的板级初始化。
2. 等 AP 本轮开机的心跳，最多 10 秒。
3. 在单独的任务里运行 `cu -l /dev/ttyAP`。
4. cu 退出后运行 CP 的 `nsh_main`。

等不到 AP 就直接留在 CP 的 NSH。回退开关：`BK7258_CONSOLE_AUTO_AP=n`，同时把 `INIT_ENTRYPOINT` 改回 `nsh_main`。CP 的 L1 校验会检查这两项是否一致。

### 考虑过的三种方案

| | A 原来的 | B 串口归 AP | C 串口归 CP、默认进 AP（采用） |
|---|---|---|---|
| 默认交互 | CP，手动 `cu` | AP | AP（自动） |
| 早期启动日志 | 完整 | 交接前完整 | 完整 |
| AP 起不来时 | CP 可用 | 要靠回退开关 | CP 可用 |
| 改动量 | — | 大：ICU 按核区分、uart 编进 AP、日志改道、串口交接、改烧录 | 小 |

选 C 的理由：

- CP 管时钟、引脚复用和 CPU1 复位，又是当前 bring-up 风险最高的核。串口归它任何时候都看得见。
- BES 也是这样做的：best2003 的 `ap` 核同时驱动 `LPUART0_SERIAL_CONSOLE` 和 Wi‑Fi，其他核用 `uart_rpmsg_init("ap", "CP", 4096, true)` 把 console 导出给它（`vendor/bes/.../src/cp.c:132`）。我们的 AP 早就这样导出过，C 只是多了开机自动 cu。

C 的代价：AP 的输入输出要多走一跳 rpmsg。等 CP 稳定、应用都在 AP 上，如果吞吐或稳定性成了问题，再考虑 B。

### 实现里的几个点

- **cu 要放在单独的任务里。** `cu_main` 会装一个 SIGINT 处理函数，退出时不恢复（`apps/system/cu/cu_main.c:350`）。如果直接在启动任务里调用，它会一直留到后面的 CP NSH。
- **`board_app_initialize` 只能执行一次。** 启动入口和 `nsh_main` 都会调用它。
- **判断 AP 就绪要比对开机序号。** 看门狗复位不会清 SWAP，上一轮留下的"AP 运行中"记录会被当真，cu 就会在 AP 起来之前执行。`bk7258_ap_is_running()` 要求记录里的序号等于 CP 这一轮准备的序号。
- **烧录脚本里注入 `~.` 行不通。** bk_loader 同时在发同步字节，cu 识别不出行首的 `~.`，`reboot bootloader` 就落到了 AP 上。手动输入 `~.` 也会受输入法影响。正确的做法是让 AP 上的 `reboot bootloader` 本身就能生效，见第三节。

## 二、两个核的 panic 现场

### 问题

AP 的 console 和 syslog 都走 RPMsg。panic 时中断已关，AP 的现场一个字也传不出去。

### 机制

1. **通知对端一起 panic。** 用 NuttX 自带的 rptun 机制：reset 回调返回 `-ENOTSUP` 时，`rptun_dev_reset`（`nuttx/drivers/rptun/rptun.c:962-1000`）会在资源表里写上 PANIC、通知对端，最多等 `RPTUN_STATUS_TIMEOUT_MS`（5 秒）。对端在 `rptun_check_peer_status`（`:750-797`）里打印 `Panic by remote core` 后 `PANIC()`。开关是 `BK7258_RPTUN_PANIC_PEER`，默认 y，每个核各自设置。
   - CP 原来的 reset 回调会直接把 CPU1 按在复位状态（`bk7258_ap_force_reset`），AP 的现场还没打出来就没了，而且是在 panic 上下文里拿互斥锁。
2. **串口令牌，单写者交接。** NuttX 串口的锁（`dev->xmit.lock`、`g_lowputs_lock`）只在本实例内有效。AMP 下两份 NuttX 互相看不见对方的锁，NuttX 也没有跨核的串口锁。SRAM 上跨核的 LDREX/STREX 语义还没验证，所以不用跨核自旋锁，改用单写者交接：
   - AP panic 时在 SWAP 第 50 字置令牌，并把 syslog 换成直接写 UART0 FIFO 的 channel。
   - CP 的 `bk7258_lowputc` 看到令牌就丢弃自己的输出。
   - AP 在 `PANIC_KERNEL_FINAL` 先 `syslog_flush()`，再释放令牌，然后才通知 CP。
   - CP 监控线程发现令牌被占超过 15 秒就强制收回，防止 AP 死在打印中途。
   - CP 每次开机、每次准备 AP 时都清令牌。
3. **停掉 Armino 的看门狗。** Armino 的中断看门狗（`CONFIG_INT_WDT_PERIOD_MS 8000`）和 AON 看门狗（10 秒）在 tick 里喂。panic 后 tick 停了，大约 8 秒整片复位，AP 那一半现场还没打完。CP 在 `PANIC_KERNEL` 阶段调 Armino 的 `bk_wdt_stop()`、`bk_aon_wdt_stop()`。我确认过链接到的是真正的驱动（`wdt_driver.c:164`、`aon_wdt_driver.c:61`），不是 `flash_shim.c` 里那个返回 `BK_FAIL` 的桩函数。
4. **方案 A 保留作兜底**：SWAP 偏移 0x100 的崩溃记录，包括文件、行号、任务名、PC/LR/SP 和 16 级 backtrace。在 `PANIC_PEER=n`，或者 AP 卡死、没有真正 panic 时有用。

### 走过的弯路：中断里 panic 时 BusFault

被通知的核是在 mailbox 中断里 panic 的。它打 backtrace 时读到非法地址，触发 BusFault。我一开始以为是 EHABI unwinder 在中断里有问题，把两个核都换成了 SP unwinder。结果 BusFault 依旧，backtrace 里还混进了假调用。

真正的根因在 NuttX 本身：

- armv8-m 的外设中断（中断号 ≥15）走 `exception_direct`（`arch/arm/src/arm_m/arm_vectors.c:125-126`，上游 `d90d45e3e76`，2024-07，"use pendsv to handle context switch"）。它只调用 `irq_dispatch(irq, NULL)`，不保存被打断的上下文。
- 所以 `g_running_task->xcp.regs` 是上一次任务切换时留下的旧值。
- `assert.c:296-301` 会检查这个 SP 是否在栈范围内，不在就改为转储整个栈，所以那次 CP 把 idle 任务的整个栈都打了出来。
- unwinder 的第二段不做这个检查，直接从旧 SP 开始读，就触发了 BusFault。

修法：两个核的 panic 回调在第一阶段调用 `bk7258_panic_fix_irq_regs()`。它从 PSP 上的硬件压栈帧（r0–r3、r12、lr、pc、xpsr）拼出被打断任务的上下文，放进一块静态缓冲，再把 `xcp.regs` 指过去。PSP 不在任务栈范围内时，把 SP 设成栈顶，这样就不扫描。panic 之后不会再切换任务，所以改写 `xcp.regs` 只影响打印现场这一段。修好以后换回 EHABI，回溯准确，也能定位到出错那一行。

另一种修法是开 `CONFIG_ARCH_RAMVECTORS`：`arm_ramvec_attach.c:89` 默认挂的是 `exception_common`，所有中断都会完整保存上下文。代价是每次中断多保存一些寄存器，向量表也要放进 RAM。这个方案没有试过。

### 现在还剩的局限

| 局限 | 来源 | 影响 |
|---|---|---|
| 寄存器块显示的是 `_assert` 自己的寄存器（PC/LR 都指向 `assert.c:904`） | NuttX：`assert.c:903-904` 无条件用 `up_saveusercontext` 覆盖（上游 `304b30f1174`，2025-12-29），所有板子都一样 | CPU 异常要看 `arm_usagefault`/`arm_busfault` 先打出的 `Fault frame: PC=...`（`arm_usagefault.c:77-90`），那个才是出错的指令 |
| 被打断任务的回溯靠板级代码补出来 | NuttX：`exception_direct` 不保存上下文，所有 Cortex-M 都一样 | 只影响中断号 ≥15 时的 panic；嵌套中断时看到的是最外层打断的任务 |
| 任务里直接 `assert` 时，当前任务只回溯出 `up_getpc`、`sched_backtrace` 两帧 | EHABI 回溯本身 | 断言的文件和行号是准的，影响不大 |

其他多核板子的做法：nrf5340、stm32h745 不开 `SCHED_BACKTRACE`，只打原始栈；BES best1700 用 `UNWINDER_STACK_POINTER`；Cortex-A 进中断时会完整保存上下文，没有第二条局限。在树内这些 Cortex-M 多核板子里，补上被打断上下文的只有我们。

看 dump 的方法：

- 只追第一段 `Assertion failed`。后崩那个核的 `rptun.c:768` 是被通知后主动 panic，不是第二个 bug。
- backtrace 用 `addr2line -f -i` 还原时，要用烧进去的那份 `nuttx.elf`，路径在 kit 的 `inputs/` 下。

## 三、重启由谁执行

### 结论：任一核的 `reboot`、`reboot bootloader` 都由 CP 整片复位

- **CP 的 `board_reset`**：所有原因都走 AON 看门狗整片复位，不再用 `up_systemreset`。
  - 普通 `reboot`：写快速启动标记 `0xAA55AA55`，BootROM 直接跳回应用。
  - `reboot bootloader`：清掉标记，并记录 WATCHDOG 原因，BL2 会打开下载窗口。
  - 这部分是从 `dev` 的 `16e61a3` 手工合进来的。
- **AP 的 `bk7258_rptun_reset`**：所有原因都返回 `-ENOTSUP`。NuttX 默认流程把原因写进资源表，CP 收到后执行 `boardctl(BOARDIOC_RESET, reason)`。只有 CP 在 5 秒内没响应时，AP 才自己调 `up_systemreset`，作为兜底。
- `60776fc` 里原先通过 SWAP 转发 `reboot bootloader` 的那套代码，被这条默认流程取代，已删除。

### 根因

| 现象 | 原因 |
|---|---|
| CP `reboot` 卡在 `Rptun driver reset remote ap` | `up_systemreset` 是 SYSRESETREQ，只复位 CPU0。CPU1、RPTUN 和共享外设的状态都还在 |
| AP `reboot` 时好时坏 | AP 只复位 CPU1。新 AP 发出的名字服务消息，可能比 CP 监控线程（每秒检查一次换代）先到，CP 按旧的 vring 状态处理，断言在 `rpmsg_virtio_buf_held_dec_test()`（`rpmsg_virtio.c:900`）。CP 先发现换代时就正常 |

第二条是我们的双核 panic 现场第一次抓到的跨核问题：CP 的现场里，rpmsg 状态显示 4 个接收缓冲全被 NS 持有，pending 为 5，比缓冲总数还多。

### 设计原则

- **核的启停只能由一个核来管。** 按 remoteproc 的设计，主核负责远端核的启动、复位和重建，远端只能提出请求。CPU1 的复位和 RPTUN 重建本来就在 CP 上（`bk7258_ap_recover_transport`）。AP 在 CP 不知情的情况下复位自己，是竞争的根源。
- **`reboot` 的含义是整机重启。** NuttX 的默认语义是：一个核要求重启，对端也跟着重启。BES 的 `reboot` 也是复位整颗芯片。
- **只重启 AP 应该是另一个功能。** 它适合量产时 AP 崩溃后的恢复，好处是 Wi‑Fi 连接和 DHCP 不用重来。如果做，也应该由 AP 提出请求、CP 执行"停掉 RPTUN → 复位 CPU1 → 重建"，并清理 CP 上属于旧 AP 的 usrsock socket 和 rpmsg 端点。`316130b` 里那条"AP 永远不复位 CP"的规则当年没写原因，这次已经去掉。

### panic 后自动重启（`8862f04`）

没有新加开关，直接用 NuttX 自带的 `BOARD_RESET_ON_ASSERT`：

| 值 | 看门狗 | 打完现场后 |
|---|---|---|
| 0（默认，开发用） | 停掉 | 停住 |
| 1 | 喂一次 | 内核 panic 后整片复位；打印中途卡死也能被看门狗救回 |
| 2 | 喂一次 | 用户任务里的 `ASSERT` 也按 panic 处理 |

AP 开了自动重启却关了 `PANIC_PEER`，会直接编译报错。因为那样 AP 只复位自己，又会撞上面那个竞争。

## 方法上的教训

- **先问清楚，再根据现象推断。** 有一次 panic 后 9–11 秒出现 `!RST`，我推断是 AON 看门狗没停住，实际上是用户手动按了复位键，等待时间正好落在那个区间。凡是可能由人操作造成的现象，先问一句。
- **第一个修法没解决问题时，回头再查根因。** BusFault 先被归到 unwinder，换了 unwinder 没好，才去查 `xcp.regs` 从哪来。如果当时就停在 SP unwinder，代价是 backtrace 里一直带着假调用。
- **主机时间戳很有用**（minicom `Ctrl-A N`）。panic 期间设备时间戳是停住的，测看门狗超时只能靠主机时间。
- **构建命令里不要写 `rm -rf`**，会触发权限确认，整条命令卡住。改成每次用新目录，代价是 `cmake_out` 下堆了一堆目录（见下）。
- **套件不要嵌套解压。** 套件目录嵌套了 33 层、路径长 1037 个字符之后，bk_loader 没写出自己的日志，`bk7258-flash.sh` 报 "loader wrote no log"，但烧录本身是成功的。在一个浅目录里解压就行。

## 未解决项

- **CP 先崩时自动重启明显比 AP 先崩慢。** 推测是在等 AP 打完现场后的确认，或者 rptun 的 5 秒超时，还没实测。
- **`full_flow.sh` 写死了 `configs/cp`/`configs/ap`**（`:198`、`:210`），编不了 `cp-armino`/`ap-armino`。这一轮都是手动调用 `build.sh`，`cmake_out/.../bk7258-wifi-native-armino/build/` 下留了十几个输出目录。等 `cp-armino`/`ap-armino` 合并成 `cp`/`ap` 之后，这个问题会自然消失。
- **只重启 AP 的恢复功能**：见第三节，尚未排期。
- **`CONFIG_ARCH_RAMVECTORS`**：可以让所有中断完整保存上下文，未试，需要先测 Wi‑Fi 中断延迟。
- **中断 syslog 缓冲溢出后强制直写**：见 `bk7258-ap-network-app-retrospective.md` 第 5 条。
