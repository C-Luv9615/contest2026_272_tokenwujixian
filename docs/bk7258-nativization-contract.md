# BK7258 原生化契约（阶段二）

本文件是「把一个 Armino 基线单元换成我们自己的实现」这件事的**契约与验收记录**。
每换一批，就在这里加一节：符号表 + 函数体要点 + 上板证据。

## 两条不变式（用户定的口径）

1. **符号不变**
   - 用 `arm-none-eabi-nm --defined-only -g <旧 .o>` 抓出基线单元的**全部**导出符号；
   - 我们的实现必须**同名、同签名、同返回语义**全部提供 —— 签名取自基线自己的头文件，不另抄声明；
   - **“今天没人调用、被 `--gc-sections` 丢掉”不是少实现符号的理由**。镜像里在不在一回事，契约是否完整是另一回事；
   - 链接映证：`nuttx.map` 里对应的 `libarmino_*.a(<unit>.c.o)` 消失，符号的提供者换成我们的 `.o`。

2. **语义不变**
   - 逐**函数体**比对：寄存器与写入顺序、返回值与错误分支、状态量、时序；
   - 不是比签名，不是比 `diff` 文本。

## 机械验收命令

```bash
# 1) 导出集合：旧 ⊆ 新
arm-none-eabi-nm --defined-only -g <旧 .o> | awk '{print $3}' | sort -u > /tmp/old.txt
arm-none-eabi-nm --defined-only -g <新 .o> | awk '{print $3}' | sort -u > /tmp/new.txt
comm -23 /tmp/old.txt /tmp/new.txt        # 必须为空

# 2) 旧成员退出镜像
grep -c 'libarmino.*(<unit>\.c\.o)' nuttx.map     # 必须为 0

# 3) 符号提供者换人
awk '/\.a\([^)]*\)[[:space:]]*$/ {prov=$NF} /^[[:space:]]+0x[0-9a-f]+[[:space:]]+[A-Za-z_][A-Za-z0-9_]*$/ {print $2"\t"prov}' nuttx.map | grep -P '^<symbol>\t'

# 4) 无未解析
arm-none-eabi-nm -u nuttx | wc -l         # 必须为 0

# 5) 段属性（RAM 常驻函数）
arm-none-eabi-objdump -t <新 .o> | grep <symbol>   # .itcm_sec_code 等要与基线一致
```

---

## 批次 1：看门狗 + 复位（`684eb2c` / 上游 `3accd85`）

替换 `wdt_driver.c`、`aon_wdt_driver.c`、`wdt_hal.c`、`aon_wdt_hal.c`、`reset_reason.c`、`reboot.c`，共 **43** 个符号，落在：

- `chips/bk7258/bk7258_wdt.c` —— 30 个（wdt_driver 17、aon_wdt_driver 5、wdt_hal 5、aon_wdt_hal 3）
- `chips/bk7258/bk7258_reset_reason.c` —— 13 个（reset_reason 11、reboot 2）

### 函数体要点

- 双看门狗：`CONFIG_INT_WDT` 8 s（硬件，喂狗间隔 `BK_MS_TO_TICKS(8000)>>4` = 500 ms）+ `CONFIG_TASK_WDT` 16 s（软件，超时 `BK_ASSERT(0)`）；
- 时间基是 **AON RTC 毫秒**（`bk_aon_rtc_get_us()/1000`），**不是**调度 tick —— 任务看门狗存在的意义就是发现“调度器停了”，不能和它共用一个时钟；
- 任务看门狗是**惰性**的：`s_last_task_wdt_feed_tick == 0` 时 `bk_task_wdt_timeout_check()` 整体不生效，直到第一次 `bk_task_wdt_feed()`。这个惰性是契约的一部分，不能“顺手 arm”；
- `.itcm_sec_code` / `__IRAM_SEC` 属性照抄（注意：本构建里 `__IRAM_SEC` 展开为空，`.itcm_sec_code` 段实际落在 flash，属性一致但没有 RAM 语义）；
- 喂狗节拍来自**我们的** `bk7258_timer.c`（TIMER_ID2 1 s 回调），这是跨单元契约；
- 复位原因写在 AON PMU R0：CP 在 bits[4:11]（8 位，>0xff 拒绝），AP 在 bits[24:30]（7 位，>0x7f 拒绝）；`reset_reason_init()` 读 R7A 锁存后把两只都清成 `RESET_SOURCE_POWERON`。

### 两个只有上板才发现的坑

1. **写 AON PMU R0 必须带 R25 两把钥匙。** `aon_pmu_hal_set_r0()` = 写 R0 → `set_r25(0x424B55AA)` → `set_r25(0xBDB4AA55)`。裸 `putreg32` 写 index 0 看起来成功、读回不变（原 board 代码就是这么写的，所以复位原因从来没记上）。证据：`wdttest reason 0x2a` 之后 `R0=R7A=0x2a0002a3`。
2. **`!RST r0=… cp=… ap=…` 不是 AON PMU R0 的读数。** 四次观察全 0，其中两次 R0 里确有值。唯一可用的读数是 `bk_misc_get_reset_reason()`（上板用 `bk7258_wdttest peek` 的 `cached=`）。

### 分层决定

芯片复位序列从 `board/` 下沉到 `chips/`：`board_reset()` 只决定“哪种 status 对应哪种复位”，寄存器归 `bk7258_chip_reset()`。**不要**用 `bk_wdt_force_reboot()` 做芯片复位：`CONFIG_NMI_WDT_EN=1` 时它走 NMI 分支并**无条件覆盖 warm tag**，会破坏需要清 tag 的 bootloader 冷启动路径。

### 上板证据（镜像 `05335514…`）

启动正常；静置数分钟不复位；`bk_wdt_stop()` 真正关狗；`reboot` 记 0x1、下次启动 `cached=0x01`；`reboot bootloader` 记 0x2、`cached=0x02`；force-reboot 整片复位后正常回来；`wlan0` 关联 + DHCP + ICMP 10/10。

---

## 批次 2：温度 / 电压检测

替换 `temp_detect.c`、`bk_sensor.c`、`volt_detect.c`，共 **26** 个符号（temp_detect 10、bk_sensor 7、volt_detect 9），落在：

- `chips/bk7258/temp_detect/bk7258_temp_detect.c` —— 采样、转换、值存储、控制面、电压 daemon
- `chips/bk7258/temp_detect/bk7258_sensor_cal.c` —— OTP 校准读取（单独 TU，原因见下）

### 函数体要点

- **OTP 校准必须保留**：`CONFIG_OTP_V1=1` 时 `bk_sensor_init()` 先读 GADC/SDMADC 校准对并编程 SARADC/SDMADC 校准寄存器。板上 SDMADC 为空（开机那行 `uncali sdmadc value` 就从这里来），但 GADC 有效，所以 SARADC 那条校准要执行；
- `bk_sensor_init()` 在 `CONFIG_PHY_MB=1` 下还要 `mb_phy_ipc_init()` + `bk_phy_server_init()`；
- 值存储是 `g_sensor_info`（beken 互斥 + `float`），未采到时是 `NAN`，读回 `BK_ERR_TRY_AGAIN`。这两个错误码是契约（`pm_misc.c`、`bk_phy_server.c` 按它判断）；
- 温度 1 s → 15 s 的间隔切换（`ADC_TMEP_DETECT_INTERVAL_CHANGE` 之后），每次采样调 `rwnx_cal_do_temp_detect()`；
- 电压采样的 **SARADC 睡眠投票顺序**：释放 → 采样 → 通知 PHY → 重新投票，必须保持；
- `temp_detect_change_configuration()` 的默认值规则（0 → 依次回落 15 s / 阈值 / dist）与“参数变化才重配”的门控要照抄。

### 值存储：真正的写入者是**闭源 `libbk_phy.a`**

第一版移植在 worker 里把原始 ADC 码（温度 560、电压 796）写进 `g_sensor_info`，上板读到
`stored_temp=559.0 stored_volt=796.0`，看着"有值"。当时在源码树里 grep 不到任何 setter 调用者，
于是得出"基线从不写入、getter 恒返回 TRY_AGAIN"的结论 —— **这个结论是错的**。

`arm-none-eabi-nm -u libbk_phy.a` 显示这个闭源库**引用**了两个 setter：

```
U bk_sensor_set_current_temperature
U bk_sensor_set_current_voltage
```

也就是说数据流是：我们采样原始码 → `rwnx_cal_do_temp_detect()` / `rwnx_cal_do_volt_detect()`
→ **闭源 PHY 换算成工程单位并回调 setter** → 值存储 → `pm_misc.c` 打印 / `bk_phy_server.c`
透传给 PHY IPC 请求。我们的第一版实现把闭源库写好的换算值**覆盖**成了原始码 —— 那才是偏差，
而且方向很坏：`pm_misc.c` 会按"温度 %.2f"打印出 559。

撤掉我们的写入后，板上读到的是闭源库自己写的值：

```
nsh> bk7258_wdttest sensor
wdttest: raw_temp=557 (ret=0) stored_temp=27.0 (ret=0) stored_volt=1.0 (ret=0) init=1
```

温度 27.0 ℃ 是这块板子的正常芯片温度；两条独立路径可以互校：OTP 出厂校准温度 569
（历史日志 97 次一致）对应的原始码比现在高 12 LSB（≈2.6 ℃），与"校准在约 29.6 ℃ 做、现在 27 ℃"
吻合。电压 1.0 是 PHY 自己的单位，本仓无法独立换算。

**教训（已写进本文件顶部的不变式之外，单独记一条）：**
"某个符号有没有调用者"必须查**闭源库**，只 grep 源码树会得到相反的答案。`nm -u <lib>.a`
是最便宜的判据，这次就是它翻案的。

### 与基线不同的两处（已确认、已记录）

1. **daemon 形态**：基线是“Beken 定时器 + 消息队列 + daemon 任务”，本移植是**自调度的 LPWORK worker**。因此 `temp_detect_send_msg()` 在 post 点就地执行，返回值语义保持（0 = 接受，`BK_FAIL` = 未建立）。
2. **3 个符号被 gc**：`temp_detect_send_msg`、`volt_daemon_restart`、`volt_daemon_stop` 在基线镜像里活着，只因为它们被 daemon 的消息分发引用；我们没有消息队列，所以它们没有引用者。**符号仍然全量提供**（26/26），只是镜像里被丢弃；另有 `bk_sensor_deinit`、`temp_detect_start` 反过来因我们的 init/deinit 路径进入了镜像。

### 编译期坑：两个 ADC 头文件家族不能进同一个 TU

- 本文件按基线 `driver/adc.h` 采样；`bk_private/bk_saradc.h`（OTP 校准时需要 `saradc_desc_t`/`saradc_set_calibrate_val`）与 `wifi/hal_port/include/driver/hal/hal_adc_types.h` 的枚举**互相重定义**；
- 更隐蔽的是：`wifi/hal_port/include/` 里也有一份 `temp_detect.h`，如果它先于基线目录被搜索到，我们的 TU 就会同时看到两套 ADC 类型。修法是在 `chips/bk7258/temp_detect/CMakeLists.txt` 里用 `target_include_directories(arch BEFORE PRIVATE …)` 把基线组件目录排到最前；
- 校准那半因此拆成独立 TU（`bk7258_sensor_cal.c`），它只包含基线头文件；`saradc_desc_t tmp_detect_desc` 也随它落位。

### 验收状态

- 26/26 导出符号、`nm -u` = 0、三个旧 archive 成员在 map 里计数为 0、静态 SRAM 287,224 B（比批次 1 后少 44 B）；
- 上板（测试镜像 `16518826…`）：`raw_temp=560 (ret=0)`，落在 `ADC_TEMP_VAL_MIN=10` ~ `ADC_TEMP_VAL_MAX=1365` 之间；`wlan0` RUNNING，`ping` 3/3；
- **定量对照（温度）**：历史日志里 `temp in otp is:569` 出现 97 次（闭源 `libbk_phy.a` 打印的 OTP 出厂校准温度，常数，不是实时读数）。今天实时 `raw_temp=560`，差 9 LSB；`ADC_TMEP_LSB_PER_10DEGREE=46` ⇒ 1 LSB ≈ 0.217 ℃ ⇒ **差约 2.0 ℃**（板子自热的量级）。两者是同一量纲、同一插值表的输入，所以这是一个定量吻合，不是“量级看起来合理”。
- **电压没有历史参照**：OTP SDMADC 为空（`uncali sdmadc value:[0 0]`，历史日志出现 90 次，与今天的行一致），所以历史上不存在电压读数可比。`raw_volt=796` 只能做区间检查（0~2048 的中段）；`_volt_detect_calculate_voltage()` 只是对 10 个样本剔掉 0/2048 后取平均，物理单位换算在闭源 `libbk_phy.a` 里，本仓无法独立换算。**这一项是未定量证明的。**
- 上板复核（测试镜像 `b0d18bad…`）：`raw_temp=557`、`stored_temp=27.0`、`stored_volt=1.0`、`ret=0`、`init=1` —— 值存储由闭源 `libbk_phy.a` 写入，与我们的采样路径一致。
