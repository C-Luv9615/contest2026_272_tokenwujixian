# BK7258 网络 bring-up 复盘

## 现在到哪了

关联、ARP、ICMP、DHCP 四层都在板上跑通过。最后一轮干净固件(无探针、无 `DEBUG_NET_INFO`)的结果:

```
CTRL-EVENT-CONNECTED, add hw key x2
dhcpc_request: Got IP address 192.168.190.248
ifconfig: inet addr:192.168.190.248 DRaddr:192.168.190.241 Mask:255.255.255.0
ping -c 4 192.168.190.241: 4 packets transmitted, 4 received, 0% packet loss
```

证据在 `evidence-20260915/bk7258-dhcp-ping-clean.log`,PSK 已脱敏。

还有三个已知问题没解决,列在最后一节。其中扫描失败率约 30%,会让任何一次板上验证有三成概率白跑,重敲一次 connect 即可。

## 根因清单

按失真层分组。每条都是一个独立机制,不能合并。

### 驱动到协议栈的交接

**1. iob 里的帧位置错了一个以太头**(`d8e3bcd`)

症状:`ifconfig` 显示 `Received=11 Errors=0`,而 `IPv4=0 ARP=0`,`arp_in` 每帧报 `Invalid hardware type`,ping 拿到 `ENETUNREACH`。

根因:`rx_submit()` 沿用 lwIP 约定,整帧从缓冲区起点开始。NuttX netdev lowerhalf 要求 `IOB_DATA` 指向 L3 载荷,以太头放在前面的预留区(`netdev.h:206-207`)。协议栈每个读者都偏了 14 字节,`arp_in()` 把目的 MAC 前两字节读成了 `ah_hwtype`。

修法:改用 `netpkt_alloc()` + `netpkt_copyin()`,让框架处理负偏移,不自己算。错误路径的 `iob_free_chain()` 一并换成 `netpkt_free()`,否则配额不归还。

判据:`arp_in` 报错归零,`ifconfig` 的 `IPv4` 计数非零,ping 通网关。

细节见 `bk7258-rx-nuttx-vs-lwip.md`。

**2. 大帧跨 IOB 链后静默丢弃**(`3b10c92`)

症状:ping 通了,DHCP 拿不到地址。三次 DISCOVER 每次 3.0 秒超时,一次 REQUEST 都没有,没有任何错误日志。

根因未确诊。已确证的是:352 字节的 OFFER 是这个驱动交出去的第一个装不进单个 IOB 的帧(`IOB_BUFSIZE` 默认 196),而 ping 验证过的 ARP(42)和 ICMP(98)都是单 IOB。

绕开:`CONFIG_IOB_BUFSIZE=640`。`640 - 14 = 626` 大于 `NET_ETH_PKTSIZE=590`,收帧不再跨链。代价是 IOB 池多占约 10.4 KB 堆。

判据:同一帧的驱动侧记录从 `io_len=182 chained=1` 变成 `io_len=338 chained=0`,DHCP 随之走完握手。

**3. netdev 的协议栈侧字段没填**(`8a6220c`)

症状:netdev 注册成功,但发帧失败,原因与 vendor 路径无关。

根因:`d_mac` 是零,carrier 从未升起,`vif_idx` 保持静态初始化的 0。

修法:在 `bk7258_wifi_lower_register()` 里从 `bk7258_wifi_sta_own_mac()` 填 `d_mac`,carrier 和 `vif_idx` 挂到关联事件上。这是 authority 侧 `low_level_init()` 填 lwIP netif 的对应动作。

### 中断上下文

**4. vendor 从硬中断调 printf**(`2d6a5eb`)

症状:`semaphore.h:518` assert。只在 `0914-vela-17` 一轮出现过,那之前 16 轮没有,那之后 7 轮也没再现。

根因:`printf()` 经 `flockfile()` 拿 stdout 的 FILE 锁,`nxmutex_wait()` 第一个 `DEBUGASSERT` 就是 `!up_interrupt_context()`。而 vendor 把 `bk_printf_ext` 注册成 Wi-Fi 和 PHY 两个 adapter 的 `._log`,MAC 硬件中断处理里会调它。

修法:换 `syslog`。`syslog_write.c:65` 在中断上下文自动降级为非阻塞,`syslog_device.c:261` 设备未就绪时返回 `-ENOSYS` 而不 assert。暴露这个 bug 的崩溃转储本身就是从中断上下文经 syslog 打出来的。

同一条规则在 `bk7258_wifi_osal_queue_send_common()` 里已经写过,这个文件漏了。

### 启动与内存

**5. PM 初始化关掉了 PSRAM 所在的电源域**(`51a3aef`)

症状:`mm_foreach()` assert,而分配器自己的记账看起来完好,`free=` 每次探测都逐字节相同。

根因:`sys_hal_power_config_default()` 把 AHBP 电源域关掉,PSRAM 是它的子模块。PSRAM 是 16.86 MiB 堆里的 16 MiB,一次寄存器写让 99.5% 的活跃堆内容失效。原先这个调用在运行时 init 里,那时 PSRAM 已经进堆一整个启动周期。

修法:PM 硬件初始化移到 `nx_start()` 之前。

**6. 链接脚本的孤儿段没被初始化**(`e74d83e`)

症状:`mb_chnl_open()` 读到 `log_chnl == 0`,无任何诊断。

根因:Armino 用 `__attribute__((section(".dtcm_sec_data")))` 标记了一些对象。这个移植只在 MEMORY 里声明了 sram 和 flash,链接器把 `.dtcm_sec_data` 当孤儿段放在 `.data` 之后:在普通 SRAM 里,但在 `_sdata.._edata` 之外。启动拷贝只走 `_sdata.._edata`,`.bss` 清零从 `_sbss` 开始,落在缝隙里的数据既不初始化也不清零。

修法:把 `.dtcm_sec_data` 折进 `.data`。

**7. 释放从核前检查了不该检查的位**(`a8321d6`)

根因:`sys_hal_power_config_default()` 通过置 halt 和 pwr_dw 把两个从核关掉,所以 `board_start_cpu()` 运行时 halt 本就是置位的,而释放流程要求它已清零。这个检查在 PM 初始化前移之后才第一次被真正执行到。

### 时序余量

**8. AP 双核 SMP 让 PBKDF2 超出 BSS 有效期**(`7d7436c`,workaround)

症状:关联必失败,扫到的 BSS 在关联工作项执行前就被回收。

根因:PBKDF2 推导 PMK 在 AP 跑 SMP 时要 12.87 秒,`BSS_EXPIRATION_AGE` 是 10 秒。关掉 AP 的 SMP 降到 2.74 秒。AP 自身在运行时调度器的 spinlock 上死锁。

这是绕开,`CONFIG_SMP=y` 是团队基线,真因解决后应该恢复。

### 可观测性

这两条不是 bug,但没有它们前面的排查都做不了。

**9. NSH 网络工具没编进去**(`bf6fb3f`):`ifconfig`/`ifup`/`renew`/`ping` 全部不可用,关联通了也没法配地址或造流量。DHCP 客户端还需要 `NET_BROADCAST` 和 `NET_SOCKOPTS` 同时开(`dhcpc.c` 用 `SO_RCVTIMEO`、`SO_BINDTODEVICE`、`INADDR_BROADCAST`,而 `udp_input.c` 的广播接收要求两者都在)。

**10. 日志噪声淹没证据**(`e7f563a`):SARADC 采样器每秒一轮,每轮无条件打四条 LOG_INFO。一次抓包里约 7% 是这些行,把 wpa_supplicant 的关联轨迹挤出了可见窗口。

## 方法上的教训

这部分比上面的清单更可复用。

**1. 探针要有独立预算。** `rx#` 探针的 24 帧预算在 52 秒就打满,而 DHCP 发生在那之后。后加的 DHCP 探针如果共用这个计数器,会静默输出零行,白一轮固件。

**2. 判据要真的能分辨两种情况。** 有一轮为了看 `No listener on UDP port` 而重编重烧,结果 0 条。回头读代码才发现 `udp_input.c:349-357` 对广播包找不到 listener 时只做 `dev->d_len = 0`,不打日志也不计数。而那一轮所有帧都是广播,0 条警告和"全被丢"、"全被收"两种情况都兼容。这一烧完全白费。

**3. 缺失的日志也是证据,但要先确认那条日志会不会打。** `parseoptions` 里四个 `nerr("Packet too short ...")` 全部静默,而 `DEBUG_NET_ERROR` 是开的,这支持 `len <= 0`(循环一次不执行)而不是内容错。反面例子是第 2 条:那次的"没有日志"什么也不说明。

**4. 自己发出去的包是天然对照组。** 广播 DISCOVER 会以回声形式收回来,里面装着 dhcpc 真正用的 xid 和 chaddr。拿它跟 OFFER 逐字段比,不需要看到 dhcpc 的私有状态,也不用改上游代码。

**5. 上游代码不改,在自己这侧取证。** `apps/netutils/dhcpc` 属于上游,按仓库规矩改它要单独发 PR。`dhcpc_parsemsg()` 的三个门任一不合就 `return 0`,任何 debug level 都打不出一行。做法是在驱动的探针里原地复现 `parseoptions` 的遍历,直接在帧上算出 `msgtype`。

**6. 烧写前先验证要看的字符串真在 ELF 里。**

```
strings nuttx.elf | grep -E "dhcp#%u dport|Received OFFER from"
```

`ninfo` 只在对应 `DEBUG_*` 打开时才编译进去,少一个配置就是一轮白烧。

**7. 烧写前先验证控制台活着。** 有一次跳过这步,连续两轮 `LinkCheck Timeout` / `GetBus fail`,最后靠拔插 USB 断电才恢复。自动 `reboot bootloader` 要求 NSH 控制台能响应,板子 assert 卡住之后按复位键不够。验证方法是发一个回车看有没有 `nsh> ` 回显,注意空闲控制台被动读是 0 字节,那个结果什么也不说明。

**8. 用 mtime 证明提交的源码就是实测的固件。** 每次提交前对比源文件 mtime 和打包时刻,能落在 commit message 里当硬证据。

**9. 一次只改一个变量。** `IOB_BUFSIZE` 那次同轮还留着 BOOTP 广播位的改动,所以在 commit 里写清了广播位不是原因(改完 OFFER 变广播,DHCP 仍然失败),只是消掉一个变量。

**10. 症状词只能当路由桶。** "ping 不通"、"关联不上" 对应过至少五个互不相干的机制。24 轮抓包里,先用计数对比把范围切开,再读代码,比直接钻源码快得多。

## 尚未解决

**跨 IOB 链丢包的机制。** 手算 `iob_copyout()` 对这个帧是正确的:偏移 28,第一个 IOB 拷 154,第二个拷 156,合计 310,正是 DHCP 报文长度。所以读代码解释不了这个失败,而三轮都是同一图形。`chain#` 探针测的是 `rx_submit` 时刻,链在那里是对的;从那里到 `udp_recvfrom` 之间还有 `netdev_iob_replace`、`eth_input`、`ipv4_input`、`udp_input`,没有测过。当前配置下这段是死代码,谁把 `NET_ETH_PKTSIZE` 提到 626 以上会再撞上。

**`uart_spinunlock` 的 assert。** 一轮 ping 中途 assert 在 `serial.c:2064`,用户栈是 ping 自己的 `printf`。`CONFIG_SPINLOCK` 和 `CONFIG_SMP` 都是关的,所以是单核下 `lock->count` 或 `sched_unlock()` 的配平问题。那一轮开着 `DEBUG_NET_INFO` 和三个探针,日志量比正常大一个数量级;删掉后没复现,但只有一轮,只能说未回归。

**扫描约 30% 失败。** 24 轮统计:`chan_survey` 打满 14 个信道则关联 15/18 成功;中途停则 0/6,`lmac_connect_req` 根本不发。失败的 6 轮里有 4 轮发生在 RX 路径改动之前,是既有问题。`iob_trimhead` 与扫描失败的相关性(见另一份文档)是同一现象的早期观察,当时也没找到机制。

**PBKDF2 仍慢一个数量级。** 关掉 AP 的 SMP 后是 2.74 秒,理论值应在 0.4 秒以内,余量只剩 7 秒多。候选是 flash XIP 取指开销和 D-cache 配了但从未启用。

**AP 的 SMP 死锁。** `7d7436c` 是 workaround,团队基线是 `CONFIG_SMP=y`。

## 复现步骤

板上,按顺序:

```
bk7258_wifi_runtime connect Luv <psk>
ifup wlan0
renew wlan0
ping -c 4 192.168.190.241
```

`connect` 约有三成概率因扫描失败而超时,看不到 `CTRL-EVENT-CONNECTED` 就重敲一次。

构建与烧录走 `.claude/skills/bk7258-worktree-build`:

```bash
scripts/full_flow.sh --workspace <workspace> --worktree <worktree> --jobs 12
scripts/full_flow.sh --workspace <workspace> --worktree <worktree> \
                     --flash --device /dev/ttyUSB0
```

烧写要交互终端,擦写 flash。日志留在 package 目录的 `bk_loader-*.log`,判据是 `Writing Flash OK`。
