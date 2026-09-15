# 全新芯片 OpenVela/NuttX Wi-Fi 适配计划

## 1. 目标与适用范围

本文定义一套面向全新芯片或新 Wi-Fi 模组的 OpenVela/NuttX 适配流程，目标是最终向系统提供标准 `wlan0`，并让上层通过 WAPI、DHCP、DNS 和 POSIX socket 使用网络，而不是直接依赖厂商私有 Wi-Fi 或 socket API。

本文同时覆盖三类硬件：

1. **片上 full-MAC Wi-Fi SoC**：MAC/PHY/RF 集成在主芯片内，通常带厂商闭源库或固件，例如 BK7258、ESP32 类平台。
2. **外置 hosted full-MAC 模组**：主控通过 SDIO、SPI 或 USB 驱动无线模组，例如 Gemini-S1 的 R528 + RTL8733BS。
3. **远端核持有 Wi-Fi**：另一个 CPU/OS 域持有 MAC/PHY，本核通过 RPMsg 使用二层帧或 socket。该方案只在硬件归属或内存约束要求时采用，不是本地 Wi-Fi 驱动的默认路径。

本文不把 Datasheet 当成 Wi-Fi 软件接口规范。Datasheet 用于确认频段、标准、供电、时钟、引脚、RF 和电气约束；MAC/PHY 固件 ABI、校准格式、OSAL 和数据路径必须来自厂商 SDK、厂商接口合同或经过授权的驱动源码。

本文的 OpenVela 网络驱动接口基线来自飞书知识库《网络驱动适配指南》（wiki token `QzBCwlIyDi9KR0kXx5scqjovnre`，docx token `OTCldequfoWVxNx1J9NcRImBn6g`，读取 revision 13）。当前源码分支的头文件和实现仍是最终事实来源；文档示例与当前源码有差异时，以当前分支编译接口为准。

## 2. 总体架构

推荐的本地 full-MAC 分层如下：

```text
应用：MQTT / HTTP / OTA / DeskMate
                  │
POSIX socket / poll / getaddrinfo
                  │
NuttX 网络栈：ARP / IPv4/IPv6 / ICMP / UDP / TCP
                  │
DHCP / DNS / netlib
                  │
WAPI / WEXT ioctl ────────┐
                  │       │
netdev upper-half         │
                  │       │
netdev_lowerhalf  +  wireless_ops_s
                  │
芯片 Wi-Fi glue：packet / event / state / error mapping
                  │
厂商 runtime：WPA / MAC / firmware / PHY / RF
                  │
NuttX OSAL + IRQ / DMA / cache / clock / power / calibration
                  │
Wi-Fi 硬件
```

稳定边界应冻结为：

- 厂商层负责扫描、关联、WPA/EAPOL、密钥、802.11 MAC、PHY、RF、校准和 802.11 与 Ethernet 帧转换。
- NuttX 负责 Ethernet 输入输出、ARP、IP、DHCP、DNS、TCP/UDP、POSIX socket 和应用协议。
- 驱动向 NuttX 数据面提交完整 Ethernet II 帧；不要让厂商 lwIP 的 IP/TCP/socket ABI 泄露给 OpenVela 应用。
- WAPI 是控制入口，不是 WPA supplicant。WPA supplicant 的归属必须在架构冻结阶段确定，并且系统中只能有一个有效实例控制同一 STA。
- 厂商内部 packet ABI 与 TCP/IP 协议栈是两个不同层次。若 MAC glue 依赖特定 `pbuf/sk_buff` 布局，应保留一个最小 vendor packet compatibility 层，并在 lower-half 边界与 NetPKT 转换；这不代表保留厂商 lwIP 的 IP/TCP/UDP/DHCP/socket。

## 3. 首先判断芯片属于哪种适配模型

### 3.1 模型 A：片上 full-MAC + 厂商 runtime

适用于 BK7258、ESP 类 SoC。需要厂商提供或允许使用：

- MAC/PHY/RF 库或固件；
- Wi-Fi 初始化、STA、扫描和事件 API；
- Ethernet TX API 与 RX callback；
- OSAL 回调表或完整未解析符号合同；
- 固件、NVRAM、efuse、校准和国家码格式；
- 编译器、CPU、FPU、CMSE/security state 和结构体 ABI；
- 二进制许可及再分发边界。

这类平台的主要工作量通常不在 `wlan0`，而在厂商 runtime 的 NuttX OSAL、内存、IRQ、cache、PHY/RF 和校准适配。

### 3.2 模型 B：外置 hosted full-MAC

适用于 Gemini-S1 的 RTL8733BS 类模组。除通用 netdev/WAPI 外，还需要：

- SDIO/SPI/USB host controller；
- 模组 power/reset/wake/OOB IRQ；
- 总线枚举、block size、DMA 和错误恢复；
- 固件下载、NVRAM/efuse/PHY 配置文件；
- 模组移除、复位和重新枚举。

### 3.3 模型 C：远端核或远端 OS 持有 Wi-Fi

只有在本核不能运行厂商 runtime 时才选用。应在以下两种边界中二选一：

- **L2 虚拟网卡**：远端保留 MAC/PHY/WPA，本核保留 NuttX IP 栈，通过 RPMsg 传完整 Ethernet 帧；控制面使用独立的 typed RPC。
- **socket proxy/usrsock**：远端保留完整 IP 栈，本核只代理 POSIX socket。

不要直接跨核传裸指针、`struct pbuf`、`struct iwreq` 或厂商结构体。协议必须包含版本、消息类型、长度、序号、状态码和明确的 buffer ownership。普通 1500-byte MTU 还要求传输一次容纳完整帧或具有可验证的分片重组。

## 4. 涉及的 NuttX/OpenVela 模块

| 层级 | 主要模块/路径 | 适配责任 |
| --- | --- | --- |
| 网络设备公共层 | `nuttx/include/nuttx/net/netdev_lowerhalf.h`、`nuttx/drivers/net/netdev_upperhalf.c` | `netdev_lowerhalf_s`、`netdev_ops_s`、`wireless_ops_s`、carrier/RX/TX 通知 |
| 网络核心 | `nuttx/net/`、`nuttx/netdev/` | netdev ioctl、ARP、IP、ICMP、UDP、TCP、路由和 socket |
| IEEE 802.11 驱动组织 | `nuttx/drivers/wireless/ieee80211/` | 可跨板复用的通用 Wi-Fi 芯片/模组驱动；若实现具有上游价值，应放到 NuttX 上游 |
| 芯片/架构层 | `nuttx/arch/<arch>/src/<chip>/` 或团队 `chips/<chip>/` | IRQ、clock/reset、cache/DMA、片上 MAC/PHY、OSAL 和芯片 glue |
| 板级层 | `nuttx/boards/...` 或团队 `board/<board>/` | power/reset GPIO、天线/PA/LNA、MAC 来源、固件/校准分区、bring-up |
| WAPI | `apps/wireless/wapi/` | 标准 scan、ESSID、BSSID、PSK/auth、connect/disconnect 用户接口 |
| 网络工具 | `apps/netutils/netlib/`、`apps/netutils/dhcpc/` | `ifup/ifdown`、静态地址、DHCP 和接口配置 |
| DNS | `nuttx/netdb/` | `getaddrinfo()`、DNS client、resolv.conf |
| 应用协议 | `apps/netutils/mqttc/` 等 | 使用标准 socket 验证网络，不进入 Wi-Fi 驱动实现 |
| 加密/认证 | 厂商 WPA、mbedTLS/crypto | WPA/EAPOL 通常归厂商 Wi-Fi runtime；TLS 属于应用层 |
| 电源管理 | 芯片 PM、board PM、Wi-Fi vendor PM | RF power、listen interval、DTIM、suspend/resume、断链唤醒 |
| 多核共享（可选） | `nuttx/drivers/rptun/`、`nuttx/drivers/net/rpmsgdrv.c`、`nuttx/drivers/usrsock/` | 仅用于远端核共享，不替代本地 Wi-Fi lower-half |

### 4.1 推荐的新驱动接口

新驱动优先实现 `netdev_lowerhalf`，不要新写旧式直接填充 `struct net_driver_s` 的驱动。

最小数据面：

```c
static const struct netdev_ops_s g_wifi_netdev_ops =
{
  .ifup     = chip_wifi_ifup,
  .ifdown   = chip_wifi_ifdown,
  .transmit = chip_wifi_transmit,
  .receive  = chip_wifi_receive,
  .reclaim  = chip_wifi_reclaim,
};
```

最小 STA 控制面：

```c
static const struct wireless_ops_s g_wifi_iw_ops =
{
  .connect    = chip_wifi_connect,
  .disconnect = chip_wifi_disconnect,
  .essid      = chip_wifi_essid,
  .bssid      = chip_wifi_bssid,
  .passwd     = chip_wifi_passwd,
  .mode       = chip_wifi_mode,
  .auth       = chip_wifi_auth,
  .country    = chip_wifi_country,
  .scan       = chip_wifi_scan,
  .range      = chip_wifi_range,
};
```

注册时使用：

```c
priv->lower.ops    = &g_wifi_netdev_ops;
priv->lower.iw_ops = &g_wifi_iw_ops;
netdev_lower_register(&priv->lower, NET_LL_IEEE80211);
```

官方指南允许无线控制采用以下两种方式之一：

- 在 `dev->iw_ops` 中实现通用 `wireless_ops_s`；
- 在 `netdev_ops_s.ioctl` 中自行处理无线 ioctl。

全新芯片优先选择 `wireless_ops_s`。只有厂商私有命令或通用操作表尚未覆盖的能力才进入普通 `ioctl`，避免重复实现 upper-half 已提供的 `SIOCSIW*` 分派。

### 4.2 数据面合同

- `transmit()` 必须非阻塞。成功接管 `netpkt` 后由驱动在 TX 完成或失败时释放，并调用 `netdev_lower_txdone()`。
- 如果暂时不能接收新的 TX，应返回可区分的负 errno 或使用内部配额/队列形成背压，不得静默丢包并返回成功。
- 厂商 RX callback 不应在硬中断中执行复杂网络处理。应创建/拷贝 RX buffer、入队，再调用 `netdev_lower_rxready()`。
- `receive()` 非阻塞地从 RX 队列返回一个 `netpkt`；没有数据时返回 `NULL`。
- 关联成功与接口 `ifup` 是两个状态。只有 WPA/密钥和数据链路真正可用后才能 `netdev_lower_carrier_on()`；断开、固件复位或 RF 关闭时必须 `carrier_off()`。
- MAC 地址、MTU、broadcast/multicast、ARP 和 1500-byte Ethernet frame 必须在开启 DHCP 前验证。
- EAPOL/WAI 应送给唯一的 WPA/认证实体，不得误送到普通 IP 数据路径。
- `quota[NETPKT_TX]` 表示驱动最多能同时持有的 TX packet 数；只有驱动调用 `netpkt_free(..., NETPKT_TX)` 后配额才恢复。若 TX 完成会及时释放，`reclaim()` 可以不实现。
- `quota[NETPKT_RX]` 表示驱动最多能预先分配并持有的 RX packet 数；packet 经 `receive()` 交回 upper-half 后配额恢复。首个正确性版本可从 TX/RX 各 1 开始，再依据吞吐测试扩容。
- TX 不能假设 `netpkt` 总是连续。若 `netpkt_is_fragmented()` 为真，使用 `netpkt_copyout()`；连续且 headroom、地址范围和硬件对齐均满足时，才允许 `netpkt_getdata()` 零拷贝。
- RX 可以在厂商 callback 中先分配、拷贝并排队，也可以在 `receive()` 中读取硬件；无论哪种方式，都必须设置正确的 L2 数据长度和 reserved/headroom，且 ISR 只做有界工作。
- 普通 Ethernet II 帧至少按 1518 字节规划。实际 `NETPKT_BUFLEN`、IOB size、厂商 TX headroom 和 DMA alignment 必须一起核算，不能只配置 MTU 1500。

### 4.3 WAPI/控制面合同

- `essid`、`passwd`、`auth`、`bssid` 建议先缓存在驱动私有状态中，最后由 `connect()` 原子提交给厂商 API。
- MVP 至少支持 `IW_MODE_INFRA`、Open/WPA2-PSK；WPA3、SoftAP、并发模式后置。
- 密码和 PMK 不得写入 syslog、崩溃转储、扫描结果或普通调试日志。
- `scan(set=true)` 异步启动扫描；`scan(set=false)` 在未完成时返回 `-EAGAIN`。
- 扫描结果缓冲不足时返回 `-E2BIG` 并更新所需长度；结果至少包含 SSID、BSSID、RSSI、信道/频率和安全类型。
- 厂商错误必须转换成稳定的 NuttX errno，例如 `-EINVAL`、`-EBUSY`、`-ETIMEDOUT`、`-ENETDOWN`、`-EACCES`。
- 最终 WAPI 验收覆盖官方指南列出的 show、scan、指定 SSID 扫描、frequency/channel、ESSID、PSK、disconnect、STA/AP mode、BSSID、save/reconnect、country 和 RSSI。阶段性 MVP 可以先实现 STA/Open/WPA2，但未覆盖项必须明确返回 `-EOPNOTSUPP`，不能假成功。

## 5. 厂商 runtime 与 OSAL 适配清单

在编写 netdev 前，先冻结厂商 runtime 的依赖矩阵。至少盘点：

| 类别 | 必须确认的合同 |
| --- | --- |
| 编译 ABI | CPU ISA、Thumb、FPU、hard/soft float、CMSE、安全域、alignment、packing、stack protector、libgcc/libc |
| 线程 | 优先级方向、栈单位、线程退出、join、自删除、ISR 上下文限制 |
| 同步 | mutex 是否递归、semaphore 初值、ISR-safe give、临界区嵌套 |
| 队列 | 元素是值还是指针、阻塞超时单位、ISR push、销毁时所有权 |
| timer/workqueue | tick 精度、周期/单次、callback 上下文、取消竞态 |
| 内存 | 普通/对齐/DMA/zeroed allocation、PSRAM 可用性、ISR allocation、峰值 |
| packet | 厂商 pbuf/skb 结构、headroom、scatter-gather、refcount、TX/RX 释放方 |
| IRQ | source、NVIC/ICU 路由、优先级、ack 顺序、shared IRQ、bottom half |
| cache/DMA | clean/invalidate 方向、cache line、descriptor ownership、内存屏障 |
| clock/reset/power | MAC/PHY/RF 时钟、复位顺序、睡眠恢复、RF coexistence |
| firmware | 加载位置、版本、hash、压缩/签名、升级兼容性 |
| calibration | efuse/NVRAM/Flash 分区、国家码、功率表、MAC 地址、工厂写入流程 |
| 许可 | 源码与二进制的链接、修改、再分发和上游提交边界 |

不得通过批量提供无语义的同名 stub 让厂商库“先链接起来”。每个 OSAL 函数都应有调用上下文、阻塞语义和资源所有权测试。

对于同时交付 vendor packet glue 和独立 TCP/IP 栈的 SDK，应进一步拆分依赖：

- **保留/兼容**：MAC 所需 packet header、private descriptor、SG、refcount、RX pool 和异步完成语义。
- **替换**：厂商 netif、IP 地址生命周期、ARP、DHCP、DNS、TCP/UDP 和 socket。
- **桥接**：关联/断开、IP-ready/DHCP-done、ARP offload/省电等跨层状态通知。

## 6. 推荐代码组织

通用芯片示例：

```text
chips/<chip>/wifi/
├── <chip>_wifi_lower.c       # netdev_ops_s + wireless_ops_s
├── <chip>_wifi_vendor.c      # 厂商 init/control/event 映射
├── <chip>_wifi_osal.c        # thread/queue/timer/lock/memory
├── <chip>_wifi_packet.c      # vendor packet ↔ NuttX netpkt
├── <chip>_wifi_hw.c          # IRQ/clock/reset/cache/DMA
├── <chip>_wifi_pm.c          # 可选，低功耗与恢复
└── <chip>_wifi_internal.h

board/<board>/src/
└── <board>_wifi.c            # power/reset、MAC、校准、国家码、天线配置
```

归属原则：

- 可在多个板复用的芯片 Wi-Fi driver 放芯片层。
- power/reset GPIO、天线、PA/LNA、固件分区和工厂参数放板级。
- 可服务多个厂商/平台的通用改动应准备独立 NuttX/OpenVela 上游 PR。
- 厂商二进制、固件和专有头文件按许可证作为受控外部输入，不应无条件复制进团队仓。

## 7. Kconfig 与构建配置

不要从一个大型量产 defconfig 整体复制配置。应从最小配置逐步启用并让 `menuconfig` 解析依赖。

### 7.1 驱动阶段

典型配置族包括：

```text
CONFIG_NET=y
CONFIG_NETDEVICES=y
CONFIG_NETDEV_IOCTL=y
CONFIG_NETDEV_WIRELESS_IOCTL=y
CONFIG_NETDEV_WIRELESS_HANDLER=y
CONFIG_DRIVERS_WIRELESS=y
CONFIG_DRIVERS_IEEE80211=y
CONFIG_NET_ETHERNET=y
CONFIG_NET_BROADCAST=y
```

芯片驱动增加自己的总开关，例如：

```text
CONFIG_<CHIP>_WIFI=y
CONFIG_<CHIP>_WIFI_STA=y
CONFIG_<CHIP>_WIFI_OSAL=y
CONFIG_<CHIP>_WIFI_DEBUG=n
```

### 7.2 IP 与工具阶段

```text
CONFIG_NET_IPv4=y
CONFIG_NET_ARP=y
CONFIG_NET_ICMP=y
CONFIG_NET_UDP=y
CONFIG_NET_TCP=y
CONFIG_NETUTILS_NETLIB=y
CONFIG_NETUTILS_DHCPC=y
CONFIG_NETDB_DNSCLIENT=y
CONFIG_WIRELESS_WAPI=y
CONFIG_WIRELESS_WAPI_CMDTOOL=y
```

实际符号随 OpenVela/NuttX 版本可能变化；以该分支 `menuconfig` 和生成 `.config` 为准。必须单独核算 `CONFIG_IOB_BUFSIZE`、IOB 数量、TCP buffer、Wi-Fi RX/TX descriptors 和任务栈，不能直接采用 Gemini-S1 的大内存配置。

## 8. 分阶段实施计划与验收门

### P0：架构与供应商输入冻结

任务：

1. 确定模型 A/B/C、CPU/安全域和 Wi-Fi 硬件所有者。
2. 冻结厂商 SDK commit、库/固件 SHA-256、工具链和许可证。
3. 取得 Wi-Fi API、OSAL、packet、firmware、校准和 PM 合同。
4. 导出预编译库未解析符号并分类，形成 ABI 矩阵。
5. 记录恢复镜像、刷写方式和失败回退方法。

完成条件：没有未知的 CPU/FPU/security ABI；能解释每个厂商库、固件和校准输入来自哪里、由谁拥有、如何更新。

### P1：内存与硬件资源基线

任务：

1. 建立 Flash、SRAM、TCM、PSRAM、DMA 和共享内存账本。
2. 记录 Wi-Fi 静态 `.text/.data/.bss`、各任务栈、packet pool、scan/WPA 峰值。
3. 验证 MAC/PHY/RF clock/reset、IRQ、DMA、cache 和校准读取。
4. 外置模组额外验证 power/reset、总线枚举、OOB IRQ 和固件下载。

完成条件：冷启动可重复完成 vendor runtime 初始化；无 netdev 时也能读取芯片/固件版本、MAC 地址和校准状态；失败路径不会死锁或破坏 console。

### P2：OSAL 合同测试

任务：

1. 实现 thread、mutex、semaphore、queue、timer、delay、critical section 和 memory wrappers。
2. 实现最小 vendor packet compatibility：packet layout、headroom/private area、chain、refcount、push/pull、coalesce 和异步释放。
3. 为可脱离硬件的 OSAL 与 packet shim 建立 host/target focused tests。
4. 在目标板执行 queue timeout、ISR give、timer cancel、线程退出、packet ownership 和内存压力测试。

完成条件：厂商 runtime 无未解析符号；所有 OSAL/packet wrapper 有语义说明；vendor packet 结构和 private descriptor 不与 NetPKT 混用；压力运行无资源泄漏、重复释放和优先级反转迹象。

### P3：RF 初始化与扫描里程碑

任务：

1. 初始化 PHY/RF 和 Wi-Fi runtime。
2. 只实现厂商 scan API 与事件接收，不急于注册完整 IP 数据面。
3. 序列化扫描结果，禁止输出凭据或未初始化内存。

硬件验收：连续扫描 20 次；能发现 2.4 GHz 测试 AP；SSID/BSSID/RSSI/channel/security 合理；无崩溃、内存持续增长或 AP/其他核失活。

### P4：STA 关联与认证里程碑

任务：

1. 实现 Open 和 WPA2-PSK；隐藏 SSID、WPA3 和 SoftAP 后置。
2. 将关联、认证成功、断开和 reason code 转换成驱动状态。
3. 验证错误密码、AP 不存在、用户取消、AP 掉电和快速重连。

完成条件：连接成功只在密钥与数据链路就绪后产生 carrier-on；每种失败均在有限时间返回稳定 errno/reason；日志不泄露密码。

### P5：`wlan0` lower-half 与静态 IP

任务：

1. 不编译厂商 lwIP netif/IP/DHCP，保留 MAC/WPA 所需 vendor packet 层。
2. 实现 `ifup/ifdown/transmit/receive/reclaim`。
3. TX 使用 `NetPKT → vendor packet → MAC`，RX 使用 `MAC → vendor packet → NetPKT`；首版采用边界拷贝，禁止把 NetPKT 强制转换为厂商 pbuf。
4. 注册 `NET_LL_IEEE80211`，提供稳定 MAC 和 MTU 1500。
5. 处理 RX/TX buffer ownership、背压、TX complete 和断链清队列。
6. 使用静态 IPv4 先验证 ARP 和 ICMP。

硬件验收：

```text
ifconfig
ifup wlan0
ifconfig wlan0 <static-ip>
ping <gateway>
```

要求验证 unicast、broadcast、ARP、1500-byte frame、连续 ping 和断线后 carrier-off。具体 CLI 以目标分支 `help` 为准。

### P6：WAPI 控制面

任务：

1. 实现 `essid/passwd/auth/mode/bssid/connect/disconnect`。
2. 实现异步 scan 和 WEXT 结果编码。
3. 实现 country/range；私有 ioctl 仅在没有标准替代时增加。

硬件验收：

```text
wapi scan wlan0
wapi disconnect wlan0
wapi essid wlan0 <ssid> 1
wapi psk wlan0 <redacted-psk> 1 3
wapi show wlan0
```

扫描、连接、断开至少各重复 20 次；错误密码不能导致永久 busy；扫描期间 connect 和 disconnect 的竞态有明确定义。

### P7：DHCP、DNS 与 socket

任务：

1. 启用 NuttX DHCP client，厂商 lwIP/DHCP 和 fast-DHCP 必须关闭。
2. NuttX DHCP lease bound 后，通过 vendor IP-ready bridge 通知 MAC；BK7258 STA 对应 `wlan_dhcp_done_ind(vif_idx)`，该通知不携带 STA MAC 地址。
3. 验证 DNS、TCP、UDP、poll 和 socket 错误传播。
4. 断开时由 NuttX 网络管理层清理或失效旧 IP、路由和 DNS；重连后重新获取地址。Wi-Fi 驱动不得调用 Armino lwIP 的 socket/netif 清理逻辑。

硬件验收：

```text
renew wlan0
ifconfig wlan0
ping <gateway>
ping <dns-name>
```

要求捕获 DHCP discover/offer/request/ack，验证 ARP 和 DNS，且 AP 掉电后现有 socket 能观察到失败。

### P8：应用协议与 TLS

任务：

1. 使用标准 POSIX socket 跑通 MQTT publish/subscribe。
2. TLS 单独验证 entropy、RTC/time sync、CA、SNI 和峰值内存。
3. 实现网络重连与 MQTT 重连的明确状态机，避免驱动层处理应用重连。

完成条件：MQTT 连续收发、broker 断开恢复和 Wi-Fi 重连均有日志与计数；凭据、证书私钥和企业数据被脱敏。

### P9：电源管理、稳健性和性能

任务：

1. 支持 suspend/resume、RF off/on、DTIM/listen interval；先验证稳定性再开启省电。
2. 注入 RF reset、IRQ 丢失、TX timeout、内存不足、AP 掉电和 firmware fault。
3. 测量吞吐、延迟、丢包、CPU、SRAM/PSRAM 峰值和功耗。
4. 执行长稳、冷启动和多次 reset cycle。

至少加入官方指南建议的 1400-byte ping，并使用 iperf2/iperf3 与 tcpdump 验证大包、吞吐和协议行为：

```text
ping -s 1400 -W 200 -c 50 -i 100 <test-host>
```

完成条件：所有错误能恢复或明确失败，不静默挂死；长稳测试记录固件版本、配置、时长、重连次数和资源水位。

### P10：SoftAP、并发和跨核共享（可选）

STA、DHCP、DNS、MQTT 稳定后，才考虑：

- SoftAP/DHCP server；
- STA+AP 并发、P2P、monitor；
- Wi-Fi/Bluetooth coexistence；
- AP/其他核通过 RPMsg L2 或 usrsock 共享网络。

这些能力应分别建任务和验收，不应混入首个 `wlan0` 里程碑。

## 9. 其他芯片的 Wi-Fi 注册实现对照

当前工作区里的实现可以分成两代注册方式：

- 旧式驱动直接维护 `struct net_driver_s`，填充 `d_ifup/d_ifdown/d_txavail/d_ioctl`，最后调用 `netdev_register(..., NET_LL_IEEE80211)`。
- 新式驱动维护 `struct netdev_lowerhalf_s`，通过 `netdev_ops_s` 提供数据面并调用 `netdev_lower_register()`；控制面还可以进一步使用 `wireless_ops_s`，由通用 upper-half 分派 WEXT/WAPI 请求。

对照结果如下：

| 平台 | 硬件形态 | 初始化与注册链 | 数据面 | WAPI/控制面 | 参考价值 |
| --- | --- | --- | --- | --- | --- |
| Gemini-S1 / RTL8733BS | R528 + SDIO hosted full-MAC | `r528_late_initialize → realtek_wlan_bringup → SDIO → realtek_wl_initialize → realtek_netdev_init → netdev_lower_register` | 新式 lower-half；`netpkt ↔ vendor skb`；使用 `rxready/txdone` | lower-half 的普通 `ioctl` 手工分派 `SIOCSIW*`，未使用 `wireless_ops_s` | 最适合参考新式数据面、packet ownership、总线/runtime/netdev 分层 |
| ESP32 / ESP32-C3 | 片上 full-MAC + vendor runtime | `board_wlan_init → esp_wlan_sta_initialize → esp_wifi_adapter_init → esp_net_initialize → netdev_register` | 旧式 `net_driver_s`；厂商 RX/TX callback + IOB/work queue | `d_ioctl` 手工分派 `SIOCSIW*` | 最适合参考片上 vendor OS adapter、MAC 获取和 RX/TX callback 注册 |
| RTL8720C AmebaZ | 片上 Realtek runtime | `amebaz_wl_initialize → amebaz_netdev_register → netdev_register → rltk_wlan_init/start` | 旧式 `net_driver_s`；`vendor skb ↔ d_buf` | `d_ioctl → rltk_wlan_control` | 适合参考 Realtek OSAL 表和厂商 skb 边界，不适合作为新注册骨架 |
| BL602 | 片上 full-MAC + Wi-Fi manager | `bl602_net_initialize → wifi_manager_process → MAC/efuse → netdev_register` | 旧式 `net_driver_s`；厂商 buffer pool | `d_ioctl` + Wi-Fi manager | 适合参考 efuse MAC、管理任务和事件状态机 |
| BCM43xxx / CYW43xxx | SDIO/gSPI hosted full-MAC | board power/reset/bus → `bcmf_sdio_initialize`/`bcmf_gspi_initialize` → firmware/NVRAM → `bcmf_driver_initialize → bcmf_netdev_register → netdev_register` | 旧式 `net_driver_s`；SDPCM/BDC frame + work queue | `d_ioctl` 手工分派 scan/auth/key/SSID | 最适合参考 firmware/NVRAM 下载、板级/总线/协议/netdev 分层和认证事件 |
| `wifi_sim` | 模拟器 | sim net lower-half 注册 + `wifi_sim_init` 挂接控制操作 | 新式 lower-half | 通用 `wireless_ops_s` | 当前分支最清晰的通用控制面语义模板，但不包含真实硬件和厂商 runtime |

### 9.1 Gemini-S1 / RTL8733BS

关键路径：

- 启动入口：`vendor/allwinnertech/chips/r528/r528_boot.c` 中的 `r528_late_initialize()`。
- SDIO 和 runtime 初始化：`vendor/allwinnertech/chips/r528/r528_wlan.c` 中的 `realtek_wlan_bringup()`。
- lower-half 操作和注册：`vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/realtek_netdev.c`。
- `realtek_netdev_init()` 为设备设置 `netdev_ops_s`，最后执行 `netdev_lower_register(dev, NET_LL_IEEE80211)`。
- TX 将 NuttX `netpkt` 转成 Realtek `sk_buff`；RX callback 将 `sk_buff` 拷贝到 RX `netpkt`，入队后调用 `netdev_lower_rxready()`。
- 控制面仍由 `realtek_ioctl()` 的 `SIOCSIWSCAN/SIOCSIWESSID/SIOCSIWAP/SIOCSIWAUTH/...` switch 处理。

因此，Gemini-S1 证明厂商 blob 完全可以位于 lower-half 之下，但它的控制面仍是兼容层写法。新芯片不应继续扩展一个巨型私有 ioctl switch，除非通用 `wireless_ops_s` 没有对应能力。

### 9.2 ESP32 / ESP32-C3

关键路径：

- 板入口：`nuttx/boards/risc-v/esp32c3/common/src/esp_board_wlan.c` 的 `board_wlan_init()`。
- STA 初始化：`nuttx/arch/risc-v/src/common/espressif/esp_wlan.c` 的 `esp_wlan_sta_initialize()`。
- OS/vendor adapter：`nuttx/arch/risc-v/src/esp32c3/esp_wifi_adapter.c`。
- `esp_net_initialize()` 填充 `struct net_driver_s` 并执行 `netdev_register(..., NET_LL_IEEE80211)`。
- 初始化后注册厂商 Ethernet RX callback 和 TX-complete callback。
- 厂商 `CONNECTED/DISCONNECTED` 事件最终映射到 `netdev_carrier_on/off()`。

对 BK7258 最重要的不是复制旧式 `net_driver_s`，而是复制这种顺序：先建立可工作的厂商 adapter 和 MAC 地址，再注册 netdev，再绑定 RX/TX callback，最后由异步连接事件控制 carrier。

### 9.3 RTL8720C AmebaZ 与 BL602

AmebaZ 在 `nuttx/arch/arm/src/rtl8720c/amebaz_netdev.c` 中直接填充旧式 `net_driver_s`，TX/RX 使用 Realtek `sk_buff`，控制请求交给 `rltk_wlan_control()`；其 RTOS 包装和 `osdep_service_ops` 位于 `amebaz_depend.c`。这对 BK7258 的 OSAL 盘点非常有参考价值。

BL602 在 `nuttx/arch/risc-v/src/bl602/bl602_netdev.c` 的 `bl602_net_initialize()` 中启动 Wi-Fi manager、读取 efuse MAC、填充 `net_driver_s` 并注册 `NET_LL_IEEE80211`。连接、扫描和 SoftAP 事件由 manager task 转换为 carrier 和同步事件。它适合参考“厂商管理任务 + netdev 事件”的边界。

两者都不应作为新驱动的注册 API 模板，因为其网络数据路径需要驱动自行持有 `d_buf`、调用 `devif_poll()` 并直接进入 IP/ARP 输入，工作量和并发风险均高于 lower-half。

### 9.4 BCM43xxx / CYW43xxx

BCM 驱动清晰地分开四层：

```text
board power/reset/clock/IRQ
        → SDIO 或 gSPI bus
        → firmware/NVRAM + SDPCM/BDC/CDC
        → bcmf netdev + WEXT
```

SDIO 板级范例位于 `nuttx/boards/arm/stm32/photon/src/stm32_wlan.c` 和 `emw3162/src/stm32_wlan.c`；它们先配置复位、电源和 IRQ，再调用 `bcmf_sdio_initialize()`。驱动在 `bcmf_sdio.c` 和 `bcmf_core.c` 中探测芯片、下载 firmware/NVRAM 并启动片内核，随后 `bcmf_driver_initialize()` 注册扫描和认证事件，最终由 `bcmf_netdev_register()` 调用旧式 `netdev_register()`。

其最值得复用的是链路状态：只有固件报告 PSK/关联成功后才 carrier-on，deauth/disassoc/link loss 则 carrier-off。这个状态机比在 `connect()` 返回时立即 carrier-on 更可靠。

### 9.5 新芯片的推荐组合

当前工作区没有一套量产驱动同时完整采用“现代 lower-half 数据面 + 通用 `wireless_ops_s` 控制面”。推荐组合参考如下：

1. **注册和 packet 数据面**：采用 Gemini-S1 的 `netdev_lowerhalf + netpkt + rxready/txdone` 模式。
2. **控制面**：采用 `wifi_sim` 展示的 `wireless_ops_s`，让 upper-half 处理标准 WEXT 分派。
3. **片上厂商 runtime/OSAL**：参考 ESP32-C3 adapter 和 AmebaZ `osdep_service_ops` 的组织方法。
4. **认证和 carrier 状态**：参考 BCM43xxx，以厂商异步认证/断开事件为唯一真值。
5. **MAC、efuse、firmware、校准**：参考 BL602、BCM43xxx 和 Gemini-S1，把运行输入与 netdev 注册解耦并建立版本/hash 合同。

推荐注册顺序：

```text
board/chip resource init
  → vendor OSAL and memory init
  → firmware/PHY/RF/calibration init
  → read and validate MAC
  → initialize driver queues and packet quotas
  → set netdev_ops_s + wireless_ops_s
  → netdev_lower_register(NET_LL_IEEE80211)
  → register vendor RX/TX/link/scan callbacks
  → interface is administratively available, carrier remains OFF
  → WAPI connect
  → vendor authentication success event
  → netdev_lower_carrier_on()
```

注册失败必须按相反顺序释放资源。若 callback 注册发生在 netdev 注册之前，则必须保证 callback 在 netdev 发布前只缓存/丢弃事件且不会访问未初始化队列；更简单的实现是先完成私有对象和 callback，再在硬件事件源仍屏蔽时注册 netdev，最后开启事件源。

## 10. Gemini-S1 参考实现

Gemini-S1 使用 R528 + Realtek RTL8733BS，SDIO1 4-bit，总体链路为：

```text
r528_late_initialize()
  → realtek_wlan_bringup()
    → sdio_initialize(1)
    → realtek_wl_sdio_init()
    → realtek_wl_initialize(0)
      → Realtek vendor runtime / firmware
      → realtek_netdev_init()
      → netdev_lower_register(..., NET_LL_IEEE80211)
        → wlan0
          → WAPI
          → renew wlan0
          → NuttX DHCP / DNS / socket
```

关键参考：

- 芯片 late init：`vendor/allwinnertech/chips/r528/r528_boot.c`。
- SDIO 与 Wi-Fi bring-up：`vendor/allwinnertech/chips/r528/r528_wlan.c`。
- lower-half/WAPI 映射：`vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/realtek_netdev.c`。
- 厂商 glue：同目录的 `realtek_driver.c`、`customer_rtos_service.c`、`osdep_service.c`。
- SDIO 层：同目录的 `realtek_sdio.c` 和 `platform/sdio/`。
- Kconfig：同目录 `Kconfig`；固件路径、MAC、efuse 和 PHY 配置均在此暴露。
- 板配置：`vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh/defconfig`。
- 用户态配网：`vendor/allwinnertech/boards/r528/r528s3-gemini-s1/src/etc/wifi/start_wifi.sh`。

可以借鉴：

- chip late-init → bus/runtime → netdev lower-half → WAPI → DHCP 的层次；
- `netdev_lower_register(NET_LL_IEEE80211)`；
- vendor skb 与 NuttX netpkt 的 buffer 边界；
- 固件、MAC、efuse/PHY 工厂参数和普通运行配置分离；
- WAPI 与 DHCP 位于用户态/网络服务层，而不是塞进 SDIO 驱动。

不能直接照搬：

- RTL8733BS 的 SDIO/OOB IRQ、固件文件名、efuse/PHY 格式；
- Realtek 私有 WEXT、`sk_buff`、双 `wlan0/wlan1` 假设；
- Gemini-S1 的大内存配置，例如大量 IOB 和超大 WAPI 栈；
- Realtek 预编译库和 OS compatibility layer；
- 启动脚本中的固定等待时间和私有命令，它们不能替代 carrier/event 驱动的状态机。

## 11. BK7258 的具体应用

当前 BK7258 CP 与 AP 都运行 OpenVela。首个 Wi-Fi 里程碑应采用模型 A：

```text
CP / NuttX
  ├── BK7258 vendor MAC/PHY/RF/WPA runtime
  ├── NuttX OSAL
  ├── bk7258_wifi lower-half
  ├── wlan0
  └── NuttX DHCP/DNS/TCP/MQTT

AP / NuttX
  └── 暂不参与首个 CP wlan0 里程碑
```

### 11.1 Armino 参考实现给出的真实启动顺序

Armino `release/v3.1.1` 的 CP Wi-Fi 初始化不是单一寄存器初始化函数，而是以下组合：

```text
app_phy_init()
  → bk_phy_adapter_init()
  → bk_rf_adapter_init()
  → 注册 PHY/RF OS/HAL 回调表

板级 vnd_cal_overlay()

app_wifi_init()
  → event init
  → vendor netif init
  → bk_wifi_init()
      → 注册 g_wifi_funcs / g_wifi_vars
      → 打开 Wi-Fi ICU gate
      → workqueue
      → MAC/PHY/PHY_WIFI power domain
      → MAC/PHY clock
      → RF vote
      → rwnxl_init()
      → calibration_init()
      → cfg_param_init()
      → rwnx core/kmsg threads
      → RX buffer
      → WPA supplicant/hostapd thread
```

关键顺序约束：`rwnxl_init()` 必须在 `calibration_init()` 之前；Armino 源码注释明确反向顺序会使校准失败。`app_phy_init()` 只注册 adapter，并不代表 MAC/PHY 已经上电。

Armino 官方 HTML 文档 `/home/czp/下载/Armino Wi-Fi 驱动程序 — 博通集成 ARMINO IDK CP 开发框架 文档.html` 明确将 Wi-Fi driver 描述为与 TCP/IP 协议栈隔离的组件。其标准 STA 流程是：

```text
EVENT_WIFI_STA_CONNECTED
  → NETIF/应用决定启动 DHCP client
  → DHCP 成功
  → EVENT_NETIF_GOT_IP4
  → 应用开始使用 socket
```

这证明将 Armino lwIP/DHCP 替换为 NuttX 网络栈/DHCPC 符合原有组件边界，不需要保留 Armino TCP/IP 栈。

### 11.2 Armino 到 NuttX 的硬件证据矩阵

下表只记录语义和验证边界，不授权复制 Armino 实现：

| 项目 | Armino/BK7258 证据 | NuttX 处置 | 证据等级 |
| --- | --- | --- | --- |
| 安全地址域 | `reg_base.h` 以 `CONFIG_SPE` 控制 `SOC_ADDR_OFFSET`；secure/non-secure alias 相差 `0x10000000` | 在链接厂商库或访问 SYS/MAC/PHY 前冻结 CP security state；禁止混用 alias | 待 boot handoff 真机确认 |
| SYS 基址 | secure profile 中 SYS 为 `0x44010000` | 复用现有 BK7258 SYS MMIO helper，增加有边界的 Wi-Fi clock/power API | 可独立实现 |
| MAC/PHY 区域 | BKRW/MAC `0x4a010000`；XVR `0x4a800000`；AGC `0x4980a000`；RC `0x4980c000`；TRX `0x4980c200`；power table `0x4980c400`；DPD `0x49840000` | 只作为厂商库和诊断所需 MMIO 区域；没有公开寄存器合同的区域不得自行初始化 | 地址可追溯，功能多数依赖厂商库 |
| 电源域 | WIFIP_MAC domain 9、WIFI_PHY domain 10；低电平字段表示 power-down | 在 `bk7258_wifi_hw.c` 实现带引用计数/状态检查的 power-on/off，验证极性 | 可独立实现，需真机确认时序 |
| 时钟门 | `SYS_CPU_DEVICE_CLK_ENABLE` 的 MAC bit 26、PHY bit 27 | 使用 read-modify-write 打开 PHY/MAC clock，保留其他设备位 | 可独立实现 |
| memory sleep/deep-sleep | MAC/PHY SD 位 21/22，DS 位 21/22；另有 MAC/PHY power sleep 位 9/10 | 首个版本不启用 Wi-Fi 低功耗；记录 bootloader 初始值，逐阶段验证 | 待真机确认 |
| reset | 当前 SDK 的 modem core/subsystem 和 MAC subsystem reset HAL 是空 TODO | 不得推断“不需要 reset”；从 TRM、Beken 支持或真机冷/热复位实验补齐 | 阻塞 PM/故障恢复，不阻塞只复用已初始化状态的试验 |
| IRQ 路由 | MODEM 29、MODEM_RC 30、TXRX_TIMER 31、MISC 32、RX 33、TX 34、PROT 35、GENERAL 36、HSU 37、WAKEUP 38 | 增加 NuttX IRQ 编号和 ICU gate；ISR handler 仍由厂商 runtime 提供或通过正式 adapter 注册 | gate 可独立实现，ISR 属厂商库 |
| ICU gate 顺序 | GENERAL→PROT→TX→RX→MISC→TIMER→MODEM；BK7258 不走 BK7236A-only MODEM_RC 分支 | 先 attach/准备 handler，再清 pending、设 priority、开 gate/NVIC，顺序需 trace 验证 | 待真机确认 |
| MAC 来源 | 默认 new MAC policy，从 SYS_NET/Flash 读取；另有 OTP2/APB fallback | 板层提供唯一稳定 MAC 读取接口，拒绝全 0/全 FF/多板重复地址 | 可独立实现 |
| RF/PHY calibration | adapter 提供 OTP/efuse/Flash/temperature/clock 回调；算法和 tag 解释位于 `libbk_phy.a` | 复用合法厂商库并实现 NuttX 回调；不得自行猜测温补、晶振和功率拟合 | 必须厂商库 |
| 国家码/功率 | 默认 CN；国家映射和功率限制为编译期表 | MVP 明确国家码来源；功率表变化需法规/射频验证 | 可接 API，数值待认证 |
| 外部 PA/LNA | GPIO26=TX_EN、GPIO28=RX_EN；`EPA_ENABLE_FLAG` 默认关闭 | 当前板若无外置 PA/LNA保持关闭；若启用，板级 pinmux 与 RF 参数单独验收 | 板级可实现，算法依赖厂商库 |
| cache/DMA | vendor adapter 暴露 `dma_memcpy` 和全 D-cache flush；可见源码没有 descriptor/cache-line 完整合同 | 首版优先保守拷贝；取得 alignment/coherency ABI 后再做 zero-copy | 待厂商合同和真机确认 |

### 11.3 哪些内容不能通过寄存器重写替代

以下能力位于 `libwifi.a`、`libbk_phy.a`、`libcom_phy.a` 或与其内部状态强耦合，不能根据 Datasheet 和可见 HAL 猜测实现：

- UMAC/LMAC 初始化和硬件状态机 `rwnxl_init()`；
- MAC/PHY ISR 核心 handler、descriptor 和 firmware/message ABI；
- RF calibration、温度补偿、晶振校准、TX power/DPD 拟合；
- 802.11 b/g/n/ax 速率、信道、聚合、重传和省电状态机；
- WPA/EAPOL 与密钥下发所依赖的 MAC 内部接口；
- Wi-Fi/Bluetooth PTA/coexistence 的完整策略。

因此 BK7258 的第一个硬件里程碑应是“厂商 runtime 在 NuttX OSAL 下完成 RF 初始化和扫描”，而不是“绕过厂商库直接写 MAC/PHY 寄存器”。如果现有 Armino 库 ABI 无法安全链接到 NuttX，必须向 Beken 获取 NuttX 版库、OSAL 合同或授权源码，再继续 netdev 适配。

### 11.4 已确认的网络栈替换边界

对 `libwifi.a` 的未解析符号审计显示：它没有直接引用 `pbuf_*`、`netif_*`、`dhcp_*`、TCP/UDP/socket 或 lwIP 外部符号；核心通过 `g_wifi_funcs`/`g_wifi_vars` callback ABI 获取平台能力。库内的 `me_*dhcp*` 用于识别 DHCP 完成状态、ARP 和省电优化，不是 DHCP client。

因此 BK7258 的正式目标边界为：

```text
保留：
  libwifi.a + libbk_phy.a (+ libbk_phy_info.a if selected)
  Beken Wi-Fi public API
  MAC data-path glue
  WPA supplicant / EAPOL path
  最小 vendor packet ABI

替换：
  Armino FreeRTOS OSAL     → NuttX OSAL
  Armino wlanif/lwIP netif → netdev_lowerhalf
  Armino lwIP IP/TCP/UDP   → NuttX network stack
  Armino DHCP/DNS/socket   → NuttX DHCPC/netdb/POSIX socket
  Armino NETIF events      → carrier + NuttX network-manager events
```

这条路径在架构上已经由 Armino 官方文档、源码调用链和二进制符号共同证明可行；剩余风险是 ABI、内存和运行时语义验证，而不是网络栈层次无法替换。**项目决策是将其作为 BK7258 Wi-Fi 的正式主路径，而不是备选试验。**

这里的“替换操作系统”包含两个独立目标：

1. FreeRTOS/Armino task、queue、timer、lock、IRQ 和 memory API 替换为 NuttX OSAL。
2. Armino lwIP 的 netif、ARP/IP、DHCP、DNS、TCP/UDP 和 socket 替换为 NuttX lower-half 与网络栈。

两者都属于本项目适配范围。保留 vendor packet ABI 只是满足 MAC 数据面的内存合同，不是保留 Armino 网络协议栈。

#### 11.4.1 文件级处置

| Armino 部件 | NuttX 计划 | 原因 |
| --- | --- | --- |
| `bk_wifi` public API、`wifi_v2.c`、`wifi_init.c` | 保留并适配 | 提供 scan/connect/STA lifecycle 和 vendor runtime 启动 |
| `rwnx_rx.c` | 保留并改造出口 | 保留 RX descriptor、802.11→802.3、A-MSDU 和 monitor；普通 Ethernet 帧改送 lower-half RX queue |
| `rw_task.c`、`rwnx_tx.c`、`rw_msdu.c` | 保留并适配 packet 入口 | 保留 MAC TX 调度、SG 和 descriptor 逻辑 |
| `wpa_supplicant-2.10`、`hostapd_intf.c` | STA MVP 保留 WPA/EAPOL 路径 | EAPOL 不是 IP 数据，必须在进入 NuttX 普通 Ethernet/IP 路径前分流 |
| `lwip_intf_v2_1/.../wlanif.c` | 不编译，以 `bk7258_wifi_lower.c` 替代 | 原文件是 lwIP netif/linkoutput/input glue |
| `bk_netif.c` | 不编译 | 其职责是启动/停止 Armino lwIP IP、DHCP 和 NETIF 事件 |
| `wifi_netif.c` | 重写为 VIF↔lower-half/event bridge | 保留 VIF/MAC 映射思想，不保留 lwIP netif/IP 生命周期 |
| lwIP `pbuf` 全栈 | 不保留协议栈；提取最小兼容 packet ABI | MAC glue 强依赖 packet layout，但 TCP/IP/DHCP 不依赖它 |

上述“保留”指在许可证允许的 vendor integration/upstream 边界内继续构建和适配，不表示把 Armino 源码直接复制进团队仓。

#### 11.4.2 Vendor packet compatibility

BK7258 当前开源 glue 对 packet 的要求不是普通 payload 指针：

- TX 遍历 `pbuf->next/payload/len/tot_len` 构造 scatter-gather。
- `rwnx_start_xmit()` 假设 `struct sk_buff` 和 TX descriptor private area 紧跟在 vendor pbuf 后方。
- RX buffer 前部包含 `fhost_rx_header`，处理后通过 push/pull 暴露完整 Ethernet II 帧。
- TX/RX 使用异步 ownership、refcount、coalesce 和 repush。

对于当前锁定的 `beken_genie` BK7258 profile：

```text
CONFIG_MSDU_RESV_HEAD_LENGTH = 108
CONFIG_MSDU_RESV_DESC_LENGTH = 600
PBUF_LINK_ENCAPSULATION_HLEN = 108 + 600 = 708 bytes
```

`PBUF_RAW_TX` allocator 会在 vendor pbuf header 与 Ethernet payload 之间预留这 708 字节；`sk_buff`、`fhost_tx_desc_tag`、额外 descriptor 和 SG `tx_pbd` 位于该区域。该数值是当前 profile 的版本化输入，不是所有 BK7258 固件的永久常量。初始化时必须以 `static_assert`/运行时检查验证 vendor header、descriptor 和 SG 数量不会越过 reserve。

因此首版必须使用独立 vendor packet：

```text
NuttX TX NetPKT
  → 分配 vendor packet（含 profile 指定的 private descriptor area）
  → copyout 完整 Ethernet frame
  → bmsg/rwnx 接受 vendor packet
  → free NetPKT + netdev_lower_txdone（lower-half quota 已恢复）
  → vendor TX complete 后独立释放 vendor packet

MAC RX vendor packet
  → rwnx RX / 802.3 conversion
  → EAPOL/WAI: vendor WPA path
  → IP/ARP/IPv6: copy into RX NetPKT
  → lower-half RX queue + netdev_lower_rxready
```

最小 shim 至少提供：alloc/free/ref、data/len/totlen/next、push/pull、append/coalesce、SG 枚举、private-area 和 cache/DMA hooks。稳定后才评估外部 buffer release callback 或零拷贝，不能让 NetPKT 直接冒充 vendor pbuf。

`transmit()` 的错误合同必须遵守 lower-half 语义：

- vendor packet 尚未成功提交时失败：释放临时 vendor packet，返回负 errno，NetPKT 仍由 upper-half 回收。
- vendor 已接管 packet：返回 `OK`；由于 Ethernet 数据已经复制完成，可以立即释放 TX NetPKT 并调用 `netdev_lower_txdone()`，但 vendor packet 必须保留到 MAC TX-complete。
- 若未来采用零拷贝，则 TX NetPKT 必须保留到真实 TX-complete，不能沿用上述立即释放路径。

内存预算必须计入每个并发 TX packet 的 708-byte profile reserve、vendor pbuf/skb、Ethernet payload 和 descriptor；不能只按 1518-byte Ethernet frame 估算。首版 TX/RX quota 从 1 开始，用吞吐与内存水位证据决定扩容。

#### 11.4.3 IP-ready 与断链桥

Armino 文档明确 `EVENT_WIFI_STA_CONNECTED` 发生在四次握手完成后，DHCP 是后续独立步骤。NuttX 映射为：

```text
EVENT_WIFI_STA_CONNECTED
  → netdev_lower_carrier_on(wlan0)
  → NuttX DHCPC
  → lease bound / IP ready
  → wlan_dhcp_done_ind(vif_idx)

EVENT_WIFI_STA_DISCONNECTED
  → netdev_lower_carrier_off(wlan0)
  → NuttX network manager 清 lease/route/DNS
  → socket 观察到网络失败并由应用决定重连
```

`wlan_dhcp_done_ind()` 的 STA 消息只携带 `vif_idx` 和 multi-protocol flag；STA MAC 只用于在原实现中查找 VIF，不需要传给固件。该通知应在 NuttX 获得可用地址后调用，以维持 Beken MAC 的 DHCP-done、ARP 和省电优化状态。首个版本关闭 `CONFIG_WIFI_FAST_DHCP`、厂商 IP mode 和厂商主动 ARP reply；这些优化必须在基础 DHCP/断链流程稳定后单独恢复。

#### 11.4.4 Beken 集成配置与编译边界

不能只把原 Armino 配置中的 `CONFIG_LWIP` 改成 `n`：当前 Beken glue 同时用它控制协议栈逻辑和 MAC RX packet 分配路径。直接关闭会走 `ke_malloc()` raw payload 分支，而其他保留代码仍按 `struct pbuf` 访问 host-id，无法形成一致合同。

应在 Beken 集成层增加明确的配置分离，名称可按上游约定调整：

```text
CONFIG_NO_HOSTED=y                 # MAC/PHY 与 NuttX 同在 CP，保持
CONFIG_BK_WIFI_NUTTX_NETDEV=y      # 新增：NuttX lower-half 出口
CONFIG_BK_WIFI_VENDOR_PACKET=y     # 新增：独立 vendor packet ABI
CONFIG_NETIF_LWIP=n                # 不创建 Armino lwIP netif
CONFIG_DHCP=n                      # 不构建/启动 Armino DHCP client
CONFIG_WIFI_FAST_DHCP=n            # 禁用厂商 fast-DHCP IP 复用
CONFIG_WIFI_VNET_CONTROLLER=n      # CP 本地 wlan0 首阶段不使用 Armino AP 虚拟网卡
CONFIG_P2P=n
CONFIG_BRIDGE=n
```

具体工作：

1. 将 `pbuf` header、allocator、chain/refcount 和 private reserve 从 `CONFIG_LWIP` 下拆到 `CONFIG_BK_WIFI_VENDOR_PACKET`。
2. `rwnx_rx.c` 的 RX host-id 始终使用一致 vendor packet，不允许同一构建中一部分路径使用 raw payload、一部分使用 pbuf。
3. 将 `wifi_v2.c`、`wifi_wpa_cmd.c` 中受 `CONFIG_LWIP` 控制的 `sta_ip_start/down` 等调用替换为 typed network bridge；不得用无语义空 stub 掩盖调用。
4. `bk_wifi_adapter.c` 中 `_sta_ip_*`、`_net_wlan_*`、`_lookup_ipaddr`、`_net_begin_send_arp_reply` 等 callback 按“需要 NuttX 等价实现 / 首阶段明确 `-EOPNOTSUPP` / 优化关闭后不可达”逐项登记和测试。
5. byte-order callback 使用标准 NuttX/libc 转换；不得因此引入 lwIP。
6. 保留 `EVENT_WIFI_STA_CONNECTED/DISCONNECTED` 和 `wlan_dhcp_done_ind()`，删除 `EVENT_NETIF_GOT_IP4` 对 Armino event task 的依赖，由 NuttX 网络管理层维护 IP-ready 状态。

当前 Armino `NO_HOSTED` 配置代表 MAC/PHY 与协议栈在同一 CPU 域，不代表必须使用 lwIP；BK7258 NuttX 仍属于 no-hosted 本地 full-MAC 架构。

#### 11.4.5 源码所有权和交付方式

`rwnx_rx.c`、`rwnx_tx.c`、`rw_task.c`、`wifi_v2.c` 等属于 Beken/Armino 集成代码。本文中的“保留并改造”指：

- 优先通过现有 callback/hook 和独立 NuttX wrapper 接入，减少对厂商数据面的修改；
- 必须修改厂商代码时，在授权的 Beken 集成仓或独立上游 PR/patchset 中完成；
- 团队仓只维护团队拥有的 NuttX lower-half、OSAL、packet bridge、板级资源和受控构建入口；
- 不把 Armino 源文件复制进团队仓并改名为团队代码。

P0 交付物必须包含一份可重放的 Beken patch manifest：上游 commit、patch hash、库 hash、配置 hash和许可证说明。

具体约束：

1. Wi-Fi 配置实际选择 `libwifi.a` + `libbk_phy.a`，并可能附加 `libbk_phy_info.a`；`libcom_phy.a` 是 Bluetooth-only 等构建分支，不应与 `libbk_phy.a` 无条件同时链接。厂商库与 thread/queue/timer、vendor packet、clock/PM/cache 和校准深度耦合，P0-P2 是硬前置条件。
2. 厂商库的 Cortex-M33、FPU、hard/soft-float、CMSE/SPE ABI 必须与 CP NuttX 对齐，不能只通过链接选项掩盖不兼容。
3. 当前 CP SRAM 窗口约 195 KiB，必须在开启 Wi-Fi 前重新核算静态区、task stack、WPA、scan、descriptor 和 packet pool；不能复制 Gemini-S1 的 buffer 配置。
4. 首阶段 CP 本地 `wlan0` 不需要 RPMsg。现有 RPTUN/RPMsg 应保持稳定，不应成为 Wi-Fi bring-up 的额外变量。
5. CP Wi-Fi 稳定后，如果 AP 也需要 POSIX 网络，再单独评估 `usrsock_rpmsg` 或 L2 虚拟网卡；不得让 AP 直接操作 CP 厂商指针或 WAPI ioctl 内存。

建议团队仓目录：

```text
chips/bk7258/wifi/
├── bk7258_wifi_lower.c
├── bk7258_wifi_vendor.c
├── bk7258_wifi_osal.c
├── bk7258_wifi_packet.c
├── bk7258_wifi_hw.c
└── bk7258_wifi_internal.h

board/bk7258-devkit/src/
└── bk7258_wifi_board.c
```

如果引入或重命名团队模块，必须同步更新仓库 manifest 的 `linkfile`。如果必须修改 NuttX 通用模块，应准备独立上游 PR，不要把上游源码复制进团队仓。

## 12. 交付物与最终验收证据

每个阶段至少提交或保存：

- 架构与接口合同；
- SDK/library/firmware/calibration 版本和 SHA-256；
- Kconfig/defconfig diff；
- 内存 map、task stack 和 packet pool 水位；
- host test 命令与结果；
- 目标构建命令与结果；
- 固件版本、打包/刷写步骤和镜像 hash；
- UART 原始日志；
- 测试 AP 配置的脱敏描述；
- scan/connect/DHCP/DNS/MQTT/断线恢复的观察结果；
- 已知限制和未通过项。

构建必须从 OpenVela workspace 根执行：

```bash
./build.sh <board-config-path> -j8
```

硬件能力不能由 host mock、goldfish 或构建成功替代。至少需要在真实目标板记录：

1. RF 初始化与扫描；
2. WPA2 关联；
3. `wlan0` carrier 与静态 IP ping；
4. DHCP 和 DNS；
5. MQTT；
6. AP 掉电/恢复和重复连接；
7. reset cycle 与长稳结果。

只有前一阶段证据完整后才进入下一阶段，避免同时调试 OSAL、RF、netdev、WAPI、DHCP 和应用协议。
