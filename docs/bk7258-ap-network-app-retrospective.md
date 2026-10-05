# BK7258 AP 侧网络应用复盘（TLS / LLM / NTP）

接在 `bk7258-network-bringup-retrospective.md` 之后：那份解决的是 CP 上 wlan0 从扫描到 ping 通，这份解决的是 AP 上的应用经 usrsock 用这条链路时遇到的问题。

## 现在到哪了

AP 上的 ai_agent 经 RPMsg usrsock 使用 CP 的 NuttX 协议栈：TLS 握手、10 KB 级请求体、LLM 回复、NTP 对时都在板上跑通。`ask` 的端到端耗时约 14 秒，其中大部分是服务端生成时间。

相关提交：

| 仓库 | commit | 内容 |
|---|---|---|
| 本仓库 | `7846afc` | TLS 单条记录上限 4096、NTP 守护任务栈 4096、`STACK_COLORATION` |
| 本仓库 | `60776fc` | AP rpmsg 工作队列栈 8192 |
| 本仓库 | `10157a9` | AP `SYSLOG_INTBUFFER` |
| `packages/ai_agent` | `6b48fe3` | `set_llm` 路径补全 |
| `apps`（本地分支 `fix/ntpclient-icmp-timeout`） | `2d34eff73` | ntpclient 连通性 ping 超时，待提上游 PR |

`apps` 那个提交只在本地。`apps` 切回别的分支，AP 上的 NTP 会悄悄失效。

## 根因清单

### 1. TLS 大写入卡死：usrsock 请求占满 rptun 环（`7846afc`）

症状：握手成功，写 10659 字节的请求体时停在 `DIAG net_send calling len=10659`，之后再无输出，也没有报错。

根因：`nuttx/drivers/usrsock/usrsock_rpmsg_server.c:455-519` 收到 sendto 请求的每个分片都 `rpmsg_hold_rx_buffer` 扣住，直到整个请求拼完才释放。AP→CP 方向只有 `VRING_NUM=4` 块、每块约 2032 字节净载，这个请求要 6 块。第 4 块被扣住后 ring 里没有空闲缓冲，AP 在 `rpmsg_get_tx_payload_buffer(wait=true)` 里无限等，CP 永远等不到剩下的分片。CP 只在用完第 `NIOVEC` 个 iov 时才报 `Request too large`，所以连这句报错都没有。

单次请求的真实上限是 `min(NIOVEC, VRING_NUM) × 约 2032` 字节，还要减去同时在发的其他 socket 占用。项目记忆里原来写的"NIOVEC=16，上限约 30 KB"是错的。

修法：`ap-armino/defconfig` 加 `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096`，一条 TLS 记录约 4.1 KB，占 3 块。`vela_tls.c` 的写循环本来就处理部分写入，所以 body 不会被截断。`IN_CONTENT_LEN` 保持 16384：服务器可以发 16 KB 的记录，而 CP→AP 方向 CP 会主动分片，没有这个问题。顺带省下约 12 KB RAM。

判据：10638 字节分成 `4117 + 4117 + 2467` 三条记录发出，服务器回包。

### 2. LLM 404：两个独立原因（`6b48fe3`）

第一个是路径补全太死。`cmd_llm.c` 只在路径正好是 `/v1` 时补 `/chat/completions`。`/compatible-mode/v1`、`/api/v3` 都被原样请求，网关返回空 body 的 404。现在的规则是：最后一段是 `v<数字>` 才补；完整路径、`/v1/messages`、`/v1beta` 都不动。规则在 `src/channels/llm_path.h`，主机测试是 `tests/host/test_llm_path.c`。

第二个是拼写错误。有一次 URL 写成了 `chat/cpmpletions`。终端没开自动换行，那一行被截断，看不到。

另外，`/api/v3/responses` 是 Responses API，请求体字段是 `input`。ai_agent 只会生成 Chat Completions 格式（`messages`），所以那次的报错是 400 `unknown field "messages"`，不是 404。

### 3. 正常回复被当成超时

症状：`LLM watchdog: call took 4183107793 ms`，回复已经收到，却被当成超时丢弃。

根因：`vela_tls.c` 发现系统时间早于 2024 年时，会在请求进行中把时钟强制拨到 2026 年。agent 用 `gettimeofday` 计时，差值就变成了约 48 天。只在开机后 NTP 同步之前的第一次请求出现。

修法：根治靠 NTP 在第一次请求前同步好，见下一条。agent 计时改成 `CLOCK_MONOTONIC` 属于上游修改，没有做。

### 4. NTP 从不同步：两个独立根因（`apps` `2d34eff73` + `7846afc`）

症状：`ntpcstatus` 一直是 0 个样本。开网络调试后，有时还会整个 AP 卡死，NSH 不能输入，`[AP] stalled`。

**4a. 连通性检查的 ping 只等 1 毫秒。** ntpclient 每轮先调 `netlib_check_ipconnectivity(NULL, 1, 1)` ping 一下 DNS 服务器，ping 不通就不发 NTP 请求。`icmp_ping` 的超时单位是毫秒：`system/ping/ping.c:47` 默认传 1000 并注释"1 second in milliseconds"，`icmp_ping.c:356` 直接把它交给 `poll`。但 `icmp_ping.h` 的注释写的是 deciseconds，ntpclient 传了 1。实际网络 RTT 是 45–59 ms，几乎每轮都失败。改成 1000，并把头文件注释改为毫秒。

**4b. NTP 守护任务栈溢出。** 守护任务栈只有 2048 字节，它自己做 DNS 解析，`lib_dnsquery.c` 在栈上放 `qname[258]` 加约 512 字节的收发缓冲。开 `STACK_COLORATION` 后 `ps` 显示实际用了 2108 字节，而可用约 1990 字节。偶尔 4a 的 ping 赶上了回包，流程走到 DNS，就会栈溢出，表现为卡死或 panic。改为 `CONFIG_NETUTILS_NTPCLIENT_STACKSIZE=4096`，现在用量 52.7%。

为什么卡死时整个 AP 都不动：守护线程从 `ntpclient.c:1290` 起整个循环都持有 `sched_lock()`。AP 是单核，线程一旦在不阻塞的状态下出问题，其他任务都排不上，包括心跳线程和 NSH。这是 ntpclient 的上游设计，我们没改。

还没处理：`NETUTILS_NTPCLIENT_SERVER` 里 `ntp1.ntsc.ac.cn`、`ntp1.cstnet.cn` 在这个网络上解析不了，现在只靠 `ntp1.aliyun.com`。

### 5. AP 在中断里写 syslog 触发断言（`10157a9`）

症状：在 mailbox 中断里执行 `syslog()` 时，断言在 `semaphore.h:518`。

根因：AP 的 syslog channel 是 RPMsg console。它的写函数要拿 `nxmutex`，中断写和普通写用的是同一个函数（`g_syslog_dev_ops`），没有中断安全的路径。

修法：`CONFIG_SYSLOG_INTBUFFER=y`、`CONFIG_SYSLOG_INTBUFSIZE=1024`。中断里的输出先放进缓冲，回到任务上下文或 panic flush 时再写出去。

剩余风险：缓冲满了以后 NuttX 改为强制直写（`syslog_intbuffer.c` 的溢出分支），又会碰到互斥锁。只有中断里短时间连续写超过 1 KB 才会触发。

### 6. AP rpmsg 工作队列栈接近满（`60776fc`）

console 移到 AP 之后，`ps` 显示 `rpmsg-cp-0` 用了 3588 / 4016 字节（89%）。`CONFIG_RPMSG_WQUEUE_STACKSIZE=8192` 之后是 11.7%。和 4b 是同一类风险，提前处理。

## 设计决策：保留 usrsock，不改 VNET

问题：AP 和 CP 之间用 usrsock（转发 socket 调用），还是用虚拟网卡（转发以太网帧，AP 跑完整协议栈）？

一度倾向 VNET，理由是"AP 也是 NuttX，usrsock 把 socket 语义切成两半"。后来查到 BES 的 Vela 多核板子用的就是 usrsock：`vendor/bes/boards/best2003_ep/aos_evb/configs/ap` 开 `NET_USRSOCK_RPMSG_SERVER` 并跑 Wi‑Fi，`audio` 开 `NET_USRSOCK_RPMSG` 做客户端。于是改为保留 usrsock。

| | usrsock | VNET |
|---|---|---|
| 跨核传什么 | socket 调用和 payload | 以太网帧 |
| TCP ACK、重传 | 在 CP 本地处理 | 每个包都要过核 |
| 大核休眠 | 友好 | 不友好 |
| 多个核共享网络 | 天然支持 | 要桥接或 NAT；STA 只有一个 MAC，桥接会被 AP 丢包 |
| socket 语义 | 部分在本地、部分转发，有缝隙 | 完全原生 |
| 现成实现 | NuttX 树内有，BES 在用 | `rpmsgdrv.c` 在这块板上撞过对称创建死锁 |

选 usrsock 的代价在第 1 条已经看到：请求大小必须装得进 rptun 环。VNET 传的是不超过 MTU 的帧，没有这个问题。另外，`SO_RCVTIMEO`、`SO_SNDTIMEO` 只在 AP 本地生效（`net/socket/setsockopt.c:89-130`），不会转发到 CP。只有在需要 raw socket、抓包这类 usrsock 转发不了的能力，或者跨核吞吐成为瓶颈时，才值得换 VNET。

## 方法上的教训

- **用对照实验切分，不要猜。**
  - 在本机用 Python 按板子的请求头重放：用假 key 得到 401，说明路由层是通的，排除了请求头的问题；用真 key 逐个去掉 `max_tokens`、`system`、`tools`，排除了请求体字段。
  - `date -s` 手动拨时间，排除了"时钟跳变导致卡死"。
  - NSH 里 `ping <域名>`，排除了 DNS 走 usrsock 有问题。
  - 每个实验只回答一个问题，结论才可靠。
- **长度推算有盲区。** 曾经从 TLS 记录长度反推出路径长度是 36 字节，判断路径已经补对了。但拼错的 `cpmpletions` 也是 36 字节，这个推算分不出来。
- **终端要开自动换行**（minicom `Ctrl-A W`）。被截断的日志行先后导致了"看不到 URL"和"看不到断言文件名"两次误判。
- **开发配置建议常开 `STACK_COLORATION`。** 4b 是看到 `ps` 的实际栈用量才坐实的，在那之前只能推断。
- **日志里会有明文 key。** `set_llm` 会把 key 原样回显到串口。抓下来的日志在提交或转发前，要先替换掉 key 和 PSK。
- **我推翻过自己的两个判断。**
  - "usrsock 不转发 `SO_SNDTIMEO`，所以写会卡住"：真正卡住的位置是等 CP 的应答，原因是第 1 条。
  - "UDP `recvfrom` 提前返回"：日志里那些"发完就关"的轮次，其实是 4a 的连通性 ping 超时。
  - 两次都是因为看到的日志不完整，就先下了结论。

## 未解决项

- `vela_tls` 等回复时忙轮询，日志里 `WANT_READ` 出现几千次，期间一直占着 CPU（`packages/ai_agent`）。
- agent 用 `gettimeofday` 计时（`packages/ai_agent`，上游问题）。
- `apps` `2d34eff73` 还没提上游 PR。
- NTP 服务器列表里两个域名解析不了。
- 中断 syslog 缓冲溢出后的强制直写路径（第 5 条的剩余风险）。
- 启动顺序（NTP 同步后再启动 ai_agent）由用户手动完成，没有做成自动；原因是联网本身有不确定性。
