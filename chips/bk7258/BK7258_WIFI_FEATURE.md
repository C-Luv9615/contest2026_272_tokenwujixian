---
feature: bk7258-wifi-porting
status: in-progress
updated: 2026-08-20
branch: feature/bk7258-wifi
commits: # filled at delivery
---

# BK7258 Wi-Fi OpenVela/NuttX 适配 — 骨架与 P0/P2 独立实现

## Report

## [S1] Problem

BK7258 DevKit 需要在 OpenVela/NuttX 上提供标准 `wlan0`，让上层通过 WAPI、DHCP、DNS 和 POSIX socket 使用网络。当前只有 Armino/FreeRTOS 版的 Beken 二进制核心（`libwifi.a`、`libbk_phy.a`），没有 NuttX 版库，vendor ABI（Cortex-M33 hard-float、CMSE、SPE/NSPE）未验证，CP SRAM 窗口约 195 KiB。

适配主路径（详见 `chips/bk7258/OPENVELA_NUTTX_WIFI_PORTING_PLAN.md` §11.4）是：保留 Beken MAC/PHY/RF 与 WPA 核心，用 NuttX OSAL 替换 FreeRTOS，用 `netdev_lowerhalf` + NuttX 网络栈替换 Armino lwIP/DHCP/socket。

本 feature 是第一个工作块：建立团队拥有的 Wi-Fi 骨架代码和 P0/P2 独立实现，不含真机验证（真机验证被 NuttX 版 Beken 库缺失阻塞）。

## [S2] Design

### 架构边界

```text
NuttX netdev_lowerhalf + wireless_ops_s   （团队实现）
        │ NetPKT
vendor packet shim（pbuf ABI + private reserve）  （团队实现）
        │
Beken libwifi.a / libbk_phy.a             （外部输入，本块不链接）
        │
BK7258 MAC / PHY / RF
```

### 团队代码目录

```text
chips/bk7258/wifi/
├── bk7258_wifi_internal.h   # 私有结构 + 接口契约 + vendor ABI 常量
├── bk7258_wifi_lower.c      # netdev_ops_s + wireless_ops_s + register
├── bk7258_wifi_osal.c       # NuttX OSAL（thread/queue/timer/lock/irq/memory）
├── bk7258_wifi_packet.c     # vendor packet shim（708-byte private reserve）
├── bk7258_wifi_hw.c         # MAC/PHY clock/power/IRQ glue（占位）
├── CMakeLists.txt           # 条件编译
├── Make.defs                # 条件编译
├── Kconfig                  # CONFIG_BK7258_WIFI 及子选项
├── ABI_AUDIT.md             # P0 产物：二进制未解析符号分类 + 库 hash
└── beken_patch_manifest.md  # P0 产物：Beken 集成 patch/库/许可证清单模板

board/bk7258-devkit/src/
└── bk7258_wifi_board.c      # 板级 MAC/校准/国家码/PA-LNA 配置（占位）
```

### 关键合同（冻结自 plan §4、§11.4）

- 注册：`netdev_lower_register(&lower, NET_LL_IEEE80211)`；数据面 `netdev_ops_s`，控制面 `wireless_ops_s`。
- 数据面首版采用边界拷贝：TX `NetPKT → vendor packet → MAC`，RX `MAC → vendor packet → NetPKT`；禁止把 NetPKT 强制转换为 vendor pbuf。
- vendor packet private reserve 常量：`CONFIG_MSDU_RESV_HEAD_LENGTH=108` + `CONFIG_MSDU_RESV_DESC_LENGTH=600` = 708 字节；以 Kconfig 暴露并在运行时/编译期断言。
- OSAL 仅映射到 NuttX 原语，禁止在 NuttX 旁运行第二个调度器；禁止批量无语义 stub。
- EAPOL/WAI 分流给 vendor WPA 路径，不进 NuttX IP 数据路径。
- 关联成功后才 `netdev_lower_carrier_on()`；DHCP lease bound 后调用 `wlan_dhcp_done_ind(vif_idx)`。
- 团队仓不复制 Armino 源码；Beken 集成修改以 patch manifest 交付。

### 构建约束

团队仓无本地 lint/unit-test/typecheck 命令（AGENTS.md）；构建在父 openvela workspace 通过 `./build.sh <board-config>` 进行，且依赖 linkfile 映射和 NuttX 版 Beken 库。本块不执行完整 NuttX 构建，也不做真机验证。

## [S3] Out of Scope

- 真机验证（RF 扫描、WPA 关联、DHCP、DNS、MQTT）——阻塞于 NuttX 版 Beken 库。
- 链接 Armino/FreeRTOS 版 `libwifi.a`/`libbk_phy.a`（ABI 未验证）。
- 修改 NuttX 上游或 Armino 集成代码（本块只产出 patch manifest 机制，不实现 patch）。
- SoftAP、P2P、STA+AP 并发、monitor、coexistence。
- RPMsg 跨核网络共享（usrsock/L2）。
- 完整 NuttX 构建与 flashing（需父 workspace linkfile 指向本 worktree + NuttX 版库）。
- board bring-up 接入 `bk7258_wifi_initialize()`（需 vendor 库可链接后；骨架阶段不加入调用点，避免未链接时产生误导性失败日志）。

## Tasks

- [ ] T1: 建立 `chips/bk7258/wifi/` 骨架与 `bk7258_wifi_internal.h` — acceptance: 私有结构、vendor packet/reserve 常量、OSAL/lower-half/hw 接口声明齐备且相互一致 (covers: S2)
- [ ] T2: 新增 `chips/bk7258/wifi/Kconfig` — acceptance: `CONFIG_BK7258_WIFI` 及子选项、vendor packet reserve 常量、vendor 库开关语义明确，可被 menuconfig 解析 (covers: S2)
- [ ] T3: 实现 `bk7258_wifi_osal.c`（NuttX OSAL 映射） — acceptance: thread/queue/timer/lock/irq/memory 每项映射到 NuttX 原语并有调用上下文语义注释，无无语义 stub (covers: S2)
- [ ] T4: 实现 `bk7258_wifi_packet.c`（vendor packet shim） — acceptance: alloc/free/ref/push/pull/coalesce/SG 枚举 + 708-byte private reserve，接口与 T1 契约一致 (covers: S2)
- [ ] T5: 实现 `bk7258_wifi_lower.c`、`bk7258_wifi_hw.c` 与板级 `bk7258_wifi_board.c` 占位 — acceptance: netdev_ops_s/wireless_ops_s/register/carrier 接口齐备，hw 门控与板级配置为明确占位而非空实现 (covers: S2)
- [ ] T6: 新增 `chips/bk7258/wifi/CMakeLists.txt` + `Make.defs` 条件编译 — acceptance: 仅在 `CONFIG_BK7258_WIFI=y` 时编入芯片组件，AP/CP 分支语义正确 (covers: S2)
- [ ] T7: 产出 P0 分析产物 `ABI_AUDIT.md` + `beken_patch_manifest.md` — acceptance: `libwifi.a`/`libbk_phy.a` 未解析符号按 OSAL/packet/协议栈/hw 分类并附库 SHA-256；manifest 模板含 upstream commit/patch hash/库 hash/许可证字段 (covers: S2; depends: T1)
