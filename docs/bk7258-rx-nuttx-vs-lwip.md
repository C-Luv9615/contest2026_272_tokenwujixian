# BK7258 接收路径:NuttX netdev 与 lwIP 的约定差异

## 摘要

vendor 交给我们的帧是正确的。驱动把帧放进 iob 的方式不对,协议栈每个读者都偏了一个以太头。

原因是两个框架的约定相反。lwIP 要求整帧从缓冲区起点开始,由 lwIP 自己剥掉以太头。NuttX 的 netdev lowerhalf 要求数据指针指向 L3 载荷,以太头放在它前面的预留区。我们的 `rx_submit` 沿用了 lwIP 那一套。

修法是改用框架自己的 `netpkt_*` API,不再手算偏移。

## NuttX netdev lowerhalf 的约定

三个宏定义了全部约定:

```c
/* nuttx/include/nuttx/net/netdev.h:206-207 */
#define IPBUF(hl)  ((FAR void *)(IOB_DATA(dev->d_iob) + (hl)))
#define NETLLBUF   ((FAR void *)((FAR uint8_t *)IPBUF(0) - NET_LL_HDRLEN(dev)))

/* nuttx/net/netdev/netdev_iob.c, netdev_iob_replace_l2() */
dev->d_len = iob->io_pktlen + NET_LL_HDRLEN(dev);
```

读出来是:

| 位置 | 内容 |
|---|---|
| `IOB_DATA(d_iob)` | L3 载荷,也就是帧的第 14 字节 |
| `IOB_DATA - 14` | 以太头,位于预留区内 |
| `io_pktlen` | 只计 L3 长度 |
| `d_len` | `io_pktlen + 14`,还原整帧长度 |

`eth_input()`(`nuttx/drivers/net/netdev_upperhalf.c:519`)因此从 `NETLLBUF` 取以太头,而 `arp_in()` 从 `ARPBUF`(等于 `IPBUF(0)`)取 ARP 头。

`NET_LL_HDRLEN` 对我们是 14:`netdev_register.c:327-328` 给 `NET_LL_IEEE80211` 设 `llhdrlen = ETH_HDRLEN`。

### 框架提供的 API 已经包含这些偏移

```c
/* netdev_upperhalf.c */
FAR netpkt_t *netpkt_alloc(dev, type)
{
  atomic_sub(&dev->quota_ptr[type], 1);   /* 配额记账 */
  pkt = iob_tryalloc(false);
  iob_reserve(pkt, CONFIG_NET_LL_GUARDSIZE);
  return pkt;
}

int netpkt_copyin(dev, pkt, src, len, offset)
{
  return iob_trycopyin(pkt, src, len,
                       offset - NET_LL_HDRLEN(&dev->netdev), false);
}
```

`netpkt_copyin` 传的是负偏移。调用者给 `offset = 0`,数据实际写到 `-14`,以太头落进预留区,`IOB_DATA` 自然指向 L3。

树内的 `igc.c` 和 `rpmsgdrv.c` 都走这条路,没有一个驱动自己算这个偏移。

## Armino 用的 lwIP 约定

```c
/* cp/components/lwip_intf_v2_1/lwip-2.1.2/port/wlanif.c */
void ethernetif_input(int iface, struct pbuf *p, uint8_t dst_idx);
```

`p->payload[0]` 是以太头的第一个字节,整帧连续存放。lwIP 的 `ethernet_input()` 自己读头,然后调 `pbuf_remove_header(p, SIZEOF_ETH_HDR)` 把头剥掉再往上递。

链路状态也不同。`low_level_init()` 把 `NETIF_FLAG_LINK_UP` 直接写进 flags 初值(`wlanif.c:137`),因为 lwIP 的 netif 是关联时动态创建的,netif 存在本身就代表链路可用。

## 两者对照

| | lwIP(Armino) | NuttX netdev lowerhalf |
|---|---|---|
| 缓冲区起点 | 以太头 | L3 载荷 |
| 以太头位置 | `payload[0]` | `IOB_DATA - 14`,在预留区 |
| 谁剥头 | lwIP 的 `ethernet_input` | 驱动在填缓冲区时就让开位置 |
| 长度字段 | `p->tot_len` 含以太头 | `io_pktlen` 不含,`d_len` 才含 |
| 链路状态 | flags 初值里的常量 | `netdev_carrier_on/off` 动态翻转 |
| 缓冲区配额 | 无 | `netpkt_alloc` 扣减,`netpkt_free` 归还 |

搬移代码时前两行最容易踩,因为两边的入口函数同名,参数也都是 `struct pbuf *`。

## 板上证据

`ethernetif_input()` 入口打印的 24 帧显示 vendor 侧完全正常:

```
rx#1   len=113/113  dst=c8:47:8c:46:02:15  et=888e  tail=02 03 00 5f   EAPOL
rx#6   len=81/81    dst=01:00:5e:00:00:fb  et=0800  tail=45 00 00 43   IPv4
rx#13  len=42/42    dst=ff:ff:ff:ff:ff:ff  et=0806  tail=00 01 08 00 06 04   ARP
```

`tail` 是偏移 14 到 19 的字节,内容正好是各协议 L3 的首字节:`45 00` 是 IPv4 的 version 4 加 IHL 5,`00 01 08 00 06 04` 是 ARP 的 hwtype、protocol、地址长度,`60` 是 IPv6 的 version 6。所以偏移标准、无 LLC/SNAP 头、每帧 `len == tot_len` 说明是单 pbuf。

同一轮的 `ifconfig` 计数暴露了错位:

```
RX: Received=0x0b  Fragment=0  Errors=0  Bytes=0x5aa
    IPv4=0  ARP=0  Dropped=0x13
```

11 帧进了协议栈,没有一个被归类。日志里的三条错误串成一条链:

```
arp_in:        ERROR: Invalid hardware type or protocol type
arp_send:      ERROR: arp_wait failed: -110, ipaddr: 192.168.190.80
icmp_sendmsg:  ERROR: Not reachable
ping:          sendto failed at seqno 0: 101      (ENETUNREACH)
```

`arp_in` 检查 `ah_hwtype != 0x0001 || ah_protocol != 0x0800`。帧里那两个字段是对的,但它从 `ARPBUF`(即 `IOB_DATA`)读,而 `IOB_DATA` 当时停在以太头开头,于是把目的 MAC 的前两字节读成了 `ah_hwtype`:广播帧读到 `0xffff`,单播帧读到 `0xc847`。ARP 回复因此全部被丢弃,`arp_send` 超时 110 秒,`icmp_sendmsg` 把它改写成 `ENETUNREACH`。

`IPv4=0` 是同一个错位的另一面:ethertype 的比较位置落在了未初始化的预留区。

### 改后:ping 通网关

同一块板,重启后连同一个 AP,ping 网关四发四收:

```
nsh> ping -c 4 192.168.190.241
56 bytes from 192.168.190.241: icmp_seq=0 time=330.0 ms
56 bytes from 192.168.190.241: icmp_seq=1 time=161.0 ms
56 bytes from 192.168.190.241: icmp_seq=2 time=79.0 ms
56 bytes from 192.168.190.241: icmp_seq=3 time=46.0 ms
4 packets transmitted, 4 received, 0% packet loss, time 4006 ms
```

这一条把整条链的双向都走通了:ARP 请求 TX(PTK 加密)、ARP 回复 RX 被 `arp_in()` 解析、ICMP echo TX、ICMP 回复 RX 递到 socket。

上一轮的那串错误全部归零:`arp_in` 的 Invalid hardware type 0 次,`arp_send` 的 arp_wait failed 0 次,`sendto failed` 0 次,`CTRL-EVENT-DISCONNECTED` 0 次(全程未断)。

日志:`evidence-20260914/bk7258-ping-gateway.log`,PSK 已脱敏。

RTT 从 330 ms 收到 46 ms。局域网 46 ms 还是偏高,和已知的性能问题同源(PBKDF2 慢一个数量级、flash XIP、D-cache 配了未启用),不阻塞。

`rx#` 探针的 24 帧预算在 52 s 就打满,ping 发生在那之后,所以日志里没有那个单播 ARP 回复的打印。ping 本身是更强的证据。

## 改动

`chips/bk7258/wifi/bk7258_wifi_lower.c` 的 `bk7258_wifi_lower_rx_submit()`:

```c
/* 改前 */
iob = iob_tryalloc(false);
iob_reserve(iob, CONFIG_NET_LL_GUARDSIZE);
iob_trycopyin(iob, vpkt->payload, len, 0, false);

/* 改后 */
iob = netpkt_alloc(PRIV2LOWER(priv), NETPKT_RX);
netpkt_copyin(PRIV2LOWER(priv), iob, vpkt->payload, len, 0);
```

错误路径上的 `iob_free_chain()` 一并换成 `netpkt_free()`,否则 `netpkt_alloc` 扣掉的配额不会归还。

长度下限也从 `len == 0` 收紧到 `len <= BK7258_ETH_HDR_LEN`。负偏移写入意味着一个不足 14 字节的畸形帧会让 `io_pktlen` 变成 0。

用 API 而不是自己 `iob_reserve` 加手动 trim,是因为偏移算术本身就是这个 bug 的来源。让框架保证约定,以后 `NET_LL_HDRLEN` 变了也不用跟着改。

## 尚未解决

### iob_trimhead 尝试与扫描失败的相关性

第一版修法是拷贝后调 `iob_trimhead(iob, 14)` 手动前移偏移。偏移算术核对无误,但连续两轮出现扫描失败:

```
Scan completed in 10.003 seconds
get scan result: empty
state=DISCONNECTED(2) reason=NO_AP_FOUND(257)
```

单独回退那一行,扫描和关联立刻恢复(`chan_survey=27`,`add hw key` 两次,`CTRL-EVENT-CONNECTED`)。

找不到机制。那两轮 `rx#` 探针零输出,说明 `ethernetif_input` 从未被调用,那行代码没有执行过;而扫描结果走管理帧路径 `rwnx_rx_mgmt_any()`,不经过 `ethernetif_input`。相同的 `chan_survey=8` 和 `10` 也出现在改动之前的抓包里。

现在的 netpkt 版本不再调用 `iob_trimhead`,所以这条路径已经不存在。但相关性没有解释,如果扫描失败再现,这是第一个要查的地方。

### wpa_supplicant 的 10 秒窗口

PBKDF2 推导 PMK 目前要 2.74 秒,而 `BSS_EXPIRATION_AGE` 是 10 秒。理论值应该在 0.4 秒以内,慢了一个数量级。AP 双核跑 SMP 时这个数字是 12.87 秒,超过阈值,扫到的 BSS 在关联工作项执行前就被回收,关联必失败。关掉 AP 的 SMP 之后降到 2.74 秒。

剩下的 2.74 秒没有解释。候选是 flash XIP 取指开销,以及 D-cache 配了但从未启用。余量只有 7 秒多,漂上去关联会重新闪断。

## 参考位置

| 内容 | 位置 |
|---|---|
| 缓冲区约定宏 | `nuttx/include/nuttx/net/netdev.h:206-207` |
| `d_len` 计算 | `nuttx/net/netdev/netdev_iob.c`,`netdev_iob_replace_l2()` |
| `netpkt_*` 实现 | `nuttx/drivers/net/netdev_upperhalf.c` |
| `eth_input` | `nuttx/drivers/net/netdev_upperhalf.c:519` |
| `arp_in` 头校验 | `nuttx/net/arp/arp_input.c:105` |
| `llhdrlen` 赋值 | `nuttx/net/netdev/netdev_register.c:327-328` |
| 树内驱动范例 | `nuttx/drivers/net/igc.c`,`rpmsgdrv.c:461` |
| lwIP 入口 | `armino cp/components/lwip_intf_v2_1/lwip-2.1.2/port/wlanif.c` |
| 我们的实现 | `chips/bk7258/wifi/bk7258_wifi_lower.c`,`bk7258_wifi_lower_rx_submit()` |
