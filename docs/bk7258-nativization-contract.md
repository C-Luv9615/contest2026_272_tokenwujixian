# BK7258 原生化契约（阶段二）

本文件是「把一个 Armino 基线单元换成我们自己的实现」这件事的**契约与验收记录**。
每换一批，就在这里加一节：符号表 + 函数体要点 + 上板证据。

## 四条不变式（用户定的口径）

1. **符号不变**
   - 用 `arm-none-eabi-nm --defined-only -g <旧 .o>` 抓出基线单元的**全部**导出符号；
   - 我们的实现必须**同名、同签名、同返回语义**全部提供 —— 签名取自基线自己的头文件，不另抄声明；
   - **“今天没人调用、被 `--gc-sections` 丢掉”不是少实现符号的理由**。镜像里在不在一回事，契约是否完整是另一回事；
   - 链接映证：`nuttx.map` 里对应的 `libarmino_*.a(<unit>.c.o)` 消失，符号的提供者换成我们的 `.o`。

2. **语义不变**
   - 逐**函数体**比对：寄存器与写入顺序、返回值与错误分支、状态量、时序；
   - 不是比签名，不是比 `diff` 文本。

3. **C 与 H 的写法原生 Vela 化**
   - `.c`：NuttX 风格（2 空格、`/* */`、花括号独占行、`#include <nuttx/config.h>` 在最前）；
   - `.h`：我们替换掉的单元，其**对外契约头也归我们**，放在 `chips/bk7258/include/`，
     用 NutxX 的 include guard 和排版；
   - 头文件只产生声明、不产生代码，所以"两边是否一致"用**预处理结果**证明，不需要新增 `.c`：
     用同一套编译参数分别预处理「只 include 权威头」和「只 include 我们的头」两个极小 TU，
     抽出涉及目标符号的声明行逐条比对（工具：`my_docs/bk7258-wifi/tools/header_equiv.py`，仓库外）；
   - 比对为**逐条相同**后，把我们的头放到与权威头**相同的相对路径**下，
     也就是芯片的 include 根本身（`chips/bk7258/include/driver/wdt.h` …），
     并在 CMake 里把该根目录排在基线 `-I` 之前 ——
     这才是"替换"，不是转发层：声明在我们文件里，权威头只是不再被先搜索到；
   - 头必须**按路径一对一替换**：`driver/wdt.h` 只放 `driver/wdt.h` 的内容、`driver/aon_wdt.h`
     只放它的内容。把两个面合并进一个头会让所有 include 点的可见声明集合变大，属于契约变更；
   - 内部 HAL 类型（`wdt_hal_t`、`gpio_hw_t`…）与私有头（`bk_wdt.h`、`wdt_driver.h`）暂不接管：
     它们是实现细节，且私有头没有"公共路径"可替换。

4. **名字按"它是什么"起，不按"它怎么来的"起**
   - 头与源文件的路径就是它替代的那个东西的路径：`chips/bk7258/include/driver/wdt.h` 就是 `driver/wdt.h`；
   - 不引入 `overrides/`、`_api`、`_shim` 这类只描述迁移动作的名字；
   - 判断标准一句：这个名字回答的是"它是什么"，还是"它怎么来的"？后者一律改掉。

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
2. **3 个符号被 gc**：`temp_detect_send_msg`、`volt_daemon_restart`、`volt_daemon_stop` 在基线镜像里活着，只因为它们被 daemon 的消息分发引用；我们没有消息队列，所以它们没有引用者。**符号仍然全量提供**（26/26），只是镜像里被丢弃；反过来 `temp_detect_start` 因我们的 `volt_daemon_init()` 调用进入了镜像（基线里它没有引用者）。

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

## 批次 3：GPIO HAL

替换 `middleware/soc/common/hal/gpio_hal.c`（+ 只给它提供 inline 的 `gpio_ll.c`），共 **27** 个符号，
落在 `chips/bk7258/bk7258_gpio_hal.c`。

### 分层

`chips/bk7258/bk7258_gpio.c` 是 **AON GPIO 块的 owner**（我们自己的驱动 + `bk_gpio_*` 面），
这一批补的是同一块的**另一个方向**：其余基线单元调用的 Armino hal API。两者都走 `gpio_ll`，
所以寄存器实现只有一份。今天唯一活着的调用点是 `pm.c` 的 `gpio_hal_switch_to_low_power_status()`
（进睡眠前把所有 GPIO 置输入态）。

### 函数体要点

- 全部 27 个函数体照抄权威实现，仍建在权威的 `gpio_ll_*` inline 上；
- 引脚存在性检查用权威的 `GPIO_DEV_MAP` 表，错误码逐一对齐：`BK_ERR_GPIO_CHAN_ID`、
  `BK_ERR_GPIO_INVALID_ID`、`BK_ERR_GPIO_INTERNAL_USED`、`BK_ERR_GPIO_NOT_OUTPUT_MODE`、
  `BK_ERR_GPIO_NOT_INPUT_MODE`、`BK_ERR_GPIOS_MAP_NONE`、`BK_ERR_GPIO_BITS_NUM`、`BK_ERR_NULL_PARAM`；
- `gpio_hal_get_output()` 在非输出模式下返回 `BK_ERR_GPIO_NOT_INPUT_MODE`（权威就是这么说反的，
  照抄，不改“更合理”的版本）；
- `gpio_hal_func_unmap()` 发现引脚被占用时**只警告并继续**，不拒绝；
- `gpio_hal_set_output_value()` / `enable_interrupt()` 都先检查方向，方向不对直接返回错误。

### 编译期坑

`GPIO_DEFAULT_DEV_CONFIG` 在 `CONFIG_GPIO_DEFAULT_SET_SUPPORT=1` 且 `CONFIG_USR_GPIO_CFG_EN=1` 时
来自 **`projects/wifi/iperf/cp/config/bk7258/usr_gpio_cfg.h`**，不是 `gpio_map.h`（后者被
`#if !CONFIG_USR_GPIO_CFG_EN` 关掉了）。所以 CP 的头文件路径里要多一条项目配置目录。

### 验收状态

- 27/27 导出符号（旧集合 ⊆ 新集合，无差异）；`gpio_hal.c.o` / `gpio_ll.c.o` 在 map 里计数为 **0**；
- `nm -u` = 0；静态 SRAM 287,224 B（与本批前一致）；
- **镜像里 `gpio_hal_*` 符号数为 0 —— 基线镜像里同样是 0**（没有任何单元真正引用它们，
  唯一调用点 `pm.c:1870` 在关闭的配置分支里），所以这与基线一致，不是回归。

### H 层的进度

| 单元 | 我们的头 | ABI 检查 |
| --- | --- | --- |
| 看门狗 / AON 看门狗 | `chips/bk7258/include/driver/{wdt,aon_wdt}.h`，芯片 include 根已排到基线之前 | ✅ 预处理逐条相同（25 + 16 条声明） |
| 复位原因 / 重启 | `chips/bk7258/include/reset_reason.h`，已排到基线之前 | ✅ 预处理逐条相同（11 条声明 + 20 条宏） |
| 温度 / 电压 | 仍用 `temp_detect.h`、`volt_detect.h` | — |
| GPIO HAL | 仍用 `gpio_hal.h`（公开 API 暴露权威定义的 ABI 结构体，见下） | — |

**为什么 `gpio_hal.h` 这类头暂时不接管**：它的 API 表面带结构体（`gpio_hal_t`、`gpio_map_t`）。
C 不允许两个不同的匿名结构体共用同一个 typedef 名，所以"我们的头与权威头逐条比对"这个判据对它们不成立；
接管只能靠"复制结构体 + 在两边 TU 里钉死 sizeof/offsetof"，保证弱一档。等这些结构体有独立类型头时再做。

切换前后 43 个符号在镜像里的"在/不在"与基线**零差异**，`nm -u` 为 0，静态 SRAM 未变。

**一条方法学补充：镜像哈希不能当作"行为未变"的证明。**
`chips/bk7258/wifi_armino/bk7258_armino_start.c:56` 里
`build_version[] = __DATE__ " " __TIME__;`，所以**任何一次真正重编都会改变镜像哈希**。
判断"改头没改行为"要看：43/26/27 个符号在镜像里的"在/不在"是否与基线一致、`nm -u` 是否为空、
`static SRAM` 是否未变 —— 本次三项全部不变（287,224 B）。

---

## 批次 4：UART（进行中）

两个单元，共 **67** 个符号：

| 单元 | 符号数 | 行数 | 状态 |
| --- | --- | --- | --- |
| `middleware/soc/common/hal/uart_hal.c` | 10 | 156 | ✅ 已替换 |
| `middleware/driver/uart/uart_driver.c` | 57 | 1754 | 待做（契约表已建，见下） |

### 分层

`chips/bk7258/bk7258_uart.c` 是**我们自己控制台驱动**（NuttX 的 UART0）。这一批补的是 Armino 侧的
`bk_uart_*` 面：`printf_base.c`、`bk_cli`、`at_server`、`ate` 这些仍在镜像里的单元调用它。
先做 HAL（寄存器层），driver 面（57 个符号）随后。

### uart_hal 的函数体要点

- 初始化顺序照抄：关中断 → 模式 → 数据位/停止位 → 校验 → 波特率 → FIFO 阈值 → 停止检测时间 →
  流控 → 唤醒配置；
- **波特率分支按权威保留**：`CONFIG_SYSTEM_CTRL` 打开时用 `UART_SCLK_APLL` 走 120 MHz 分支，
  而 `#else` 分支比较的是 `UART_SCLK_DCO` —— 看着别扭，但这是硬件一直在跑的那条；
- 流控极性固定写 1/1、低水位固定 1；
- `uart_hal_get_system_interrput_en_status()`（权威就拼错了 interrupt）按 UART_ID_0/1/2 读
  `sys_ll_get_cpu0_int_0_31_en_*`，其它 id 返回 0。

### 控制台驱动改用同一层（第 1 步，已完成）

`chips/bk7258/bk7258_uart.c` 原本自己 `putreg32/getreg32/modifyreg32` 操作 UART0 寄存器，
现在 **12 处走 `uart_ll_*`**：

| 原操作 | 现在 |
| --- | --- |
| `modifyreg32(UART_CONFIG, … RX_EN)` | `uart_ll_enable_rx()` |
| `putreg32(0, UART_INT_ENABLE)` ×2 | `uart_ll_set_int_enable_status(…, 0)` |
| `putreg32(priv->ie, UART_INT_ENABLE)` ×3 | `uart_ll_set_int_enable_status(…, priv->ie)` |
| `getreg32(UART_FIFO_PORT) >> 8` | `uart_ll_read_byte()` |
| `FIFO_STATUS & RD_READY` / `WR_READY` / `bit17` | `uart_ll_is_fifo_read_ready()` / `is_fifo_write_ready()` / `is_tx_fifo_empty()` |
| `getreg32/putreg32(UART_INT_STATUS)` | `uart_ll_get_interrupt_status()` / `uart_ll_clear_interrupt_status()` |

剩下 2 处是 **SoC 中断路由**（`BK7258_SYS_CPU0_INT_EN`），属于另一个块、`uart_ll` 没有对应接口，保持原样。

替换前逐一核对过偏移与位定义与 `middleware/soc/bk7258/soc/uart_struct.h` 一致；
反汇编确认 `bk7258_uart_receive` 仍是 `ldr r0, [r3, #28]`（0x1c = FIFO port），与替换前同一寄存器。

### 验收（uart_hal）

- 10/10 导出符号，旧集合 ⊆ 新集合，无差异；
- `uart_hal.c.o` 在 `nuttx.map` 里计数 **0**；
- 镜像里 `uart_hal_*` 符号 **7 个，与基线完全相同**（其余 3 个在基线里也被 gc）；
- `nm -u` 为 0；静态 SRAM 287,224 B（未变）；43 个 WDT/reset 符号面零差异。

### uart_driver 的 57 个符号：契约表

**它不是死代码。** `bk_uart_driver_init()` 在 `middleware/driver/common/driver.c` 里被调用，而且
Armino 的日志通路就是它：

```
bk_printf / BK_LOGx  →  components/bk_system/printf.c + printf_base.c
        →  bk_uart_write_byte / uart_write_string   ← uart_driver.c
        →  uart_ll  →  寄存器
```

板上那些 `wifi:I(...)`、`wifi9d:I(...)`、`cal:W(...)` 行就是这么出来的，所以我们自己控制台的
`/dev/console` 与它共用同一个 UART0 硬件。**移植错了会先丢日志，再丢控制台。**

按调用者是否真在镜像里分组（`.o` 出现在 `nuttx.map` 才算）：

| 组 | 符号（节选） | 镜像内调用者 |
| --- | --- | --- |
| **主路径**（初始化/配置/读写） | `bk_uart_driver_init/deinit`、`bk_uart_init/deinit`、`bk_uart_set_baud_rate/data_bits/parity/stop_bits`、`bk_uart_set_enable_rx/tx`、`bk_uart_read_bytes`、`bk_uart_write_bytes`、`bk_uart_wait_tx_over`、`bk_uart_is_tx_over`、`uart_write_byte`、`uart_write_string` | `driver.c`、`printf_base.c`、`printf.c`、`command_line.c` |
| **中断面** | `bk_uart_register_rx_isr/tx_isr`、`bk_uart_take_rx_isr`、`bk_uart_recover_rx_isr`、`bk_uart_enable_rx/tx_interrupt`、`bk_uart_disable_rx/tx_interrupt`、`uart0/1/2_isr`、`uart_get_interrupt_status`、`uart_get_int_enable_status`、`uart_clear_interrupt_status` | `cli_main.c`、`shell_task.c` |
| **FIFO / 软 FIFO** | `bk_uart_enable_sw_fifo`、`bk_uart_disable_sw_fifo`、`uart_get_length_in_buffer`、`bk_uart_set_rx_full_threshold`、`bk_uart_set_tx_empty_threshold`、`bk_uart_set_rx_timeout` | `cli_main.c`、`cli_uart.c`（后者不在镜像里） |
| **PM 钩子** | `bk_uart_pm_backup`、`bk_uart_pm_restore`、`uart_id_to_pm_uart_id` | `shell_task.c`、`at_task.c`；前两者在本构建里是 `return BK_OK` 空壳（`CONFIG_UART_PM_CB_SUPPORT` 未定义，反汇编已证） |
| **ATE / FR 旁路** | `uart_write_byte_for_ate`、`uart_write_byte_for_fr`、`bk_uart_get_ate_detect_gpio`、`bk_uart_get_rx_gpio` | `ate.c`、`at_task.c` |
| **DMA** | `bk_uart_is_rx_dma_enabled` | 无外部调用者 |
| **杂项** | `bk_uart_disable_rx/tx`、`bk_uart_disable_hw_flow_ctrl`、`bk_uart_set_hw_flow_ctrl`、`bk_uart_is_in_used`、`uart_read_byte`、`uart_read_byte_ex`、`uart_read_ready`、`uart_write_ready`、`uart_clock_enable/disable`、`uart_wait_tx_over`、`print_hex_dump` | `print_hex_dump` 被 wpa 的 `ctrl_iface.c` 用；其余无外部调用者 |

必须保留的语义要点（实现时逐条对）：

1. **`bk_uart_driver_init()` 幂等**，首次调用时初始化 ISR 表、统计并从 `bk_printf_init()` 起日志通路；
2. `bk_uart_init()` 通过 `uart_hal_init_uart()` 下发配置，`bk_uart_deinit()` 走 `uart_hal_stop_common()`；
3. **TX 阻塞语义**：`bk_uart_write_bytes()` 与 `bk_uart_wait_tx_over()`/`bk_uart_is_tx_over()` 的"等 FIFO 空"判据要一致（日志行不能截断）；
4. **ISR 注册表**：`bk_uart_register_rx_isr(id, fn, arg)` 的回调签名与调用上下文（中断里、不清中断状态由驱动做）；
5. **软 FIFO**：`enable_sw_fifo` 之后 `uart_get_length_in_buffer()` 的读数语义；
6. **ATE 旁路**：`uart_write_byte_for_ate/for_fr` 用的不是同一个写路径（历史遗留），别合并；
7. `print_hex_dump` 是 wpa 的调试输出，输出格式（每行字节数、前缀）必须一致。

### uart_driver 的实施顺序（每段独立可回滚）

1. 主路径 + 中断面（日志与控制台必需）；
2. FIFO/软 FIFO + 杂项；
3. DMA / ATE / FR / PM 空壳；
4. 全 57 个到位后，`uart_driver.c` 从 `excluded_units` 出局。

---

## 批次 5：日志传输（`printf.c` + `printf_base.c`）

26 个符号（`printf.c` 20 + `printf_base.c` 6），落在 `chips/bk7258/bk7258_printf.c`。

### 为什么从这一层动手

`BK_LOGx` 展开成 `bk_printf_ext(level, tag, fmt, ...)`，而**两个闭源库也直接调它**
（`libwifi.a` 引用 `bk_printf_ext`/`bk_printf_raw`；`libbk_phy.a` 另加 `bk_get/set_printf_sync`）。
所以这一层是**所有厂商日志的唯一入口**，包括闭源库内部的日志。

原链路（`CONFIG_SHELL_ASYNCLOG=1`）：

```
BK_LOGx → bk_printf_ext → printf.c 组前缀 → shell_log_out_port（bk_cli 环形缓冲）
        → shell_log_flush → bk_uart_write_bytes → uart_driver → uart_ll
```

新链路：

```
BK_LOGx → bk_printf_ext（我们的）→ 组同样的前缀 → syslog()
```

于是：日志通道收敛到 NuttX；`printf.c`/`printf_base.c` 出局；**`uart_driver` 不再是日志的必经之路**。

### 保住了什么

- 线上格式不变：`[cpu0:][tag:]E|W|I|D|V(ms):`（历史日志可比）；
- 级别过滤、按模块禁用与白名单（`bk_disable_mod_printf`/`bk_enable_white_list`/`bk_white_list_state`）、
  `bk_set_printf_enable`、`bk_set/get_printf_sync`、`CONFIG_PRINTF_BUF_SIZE`（256）截断；
- `printf_is_init` 门控、`bk_printf_deinit` 里 `printf_lock_deinit()` 的调用顺序。

### 有意改变的一处

`bk_printf_init()` **不再调用 `bk_uart_init()`**。UART0 归 NuttX 控制台驱动（`bk7258_uart.c`），
日志层再去初始化一次会把控制台的端口重新编程。`bk_get/set_printf_port()` 仍记录端口号（API 语义），
但不再触碰硬件。

### 验收

- 26/26 导出符号（旧集合 ⊆ 新集合）；`printf.c.o`/`printf_base.c.o` 在 map 里计数 **0**；
- `nm -u` 为 0；静态 SRAM 287,136 B（比替换前少 88 B）；
- 43 个 WDT/reset 符号面零差异；
- 上板后发现并修掉两处：前缀时间戳单位（`CONFIG_USEC_PER_TICK=1000`，tick 即毫秒，初版误除成秒）与
  `bk_sensor_init()` 重复调用（`components_init.c:271` 已拥有，我们在 `temp_detect_init` 里又调了一次，
  导致 `uncali sdmadc value` 每次启动打印两遍）；
- **一处镜像差异**：`printf_lock_deinit` 不在镜像里（导出仍在 `.o` 里）。原因是它函数体无副作用，
  GCC 把唯一调用优化掉后符号被收集；基线里它活着只是因为那次调用跨 TU、编译器无法判定。
  不为一个 map 条目添加假的副作用。
- 待上板：日志仍按要求格式出现、控制台正常。

### uart_driver 移植工作表（源码已通读，实现未开始）

**本构建实际编译的部分**（用于估算工作量，**不是裁剪依据**）：`CONFIG_UART_RX_DMA` / `TX_DMA` 未定义 ⇒
DMA 路径不编译；`CONFIG_UART_PM_CB_SUPPORT` 未定义 ⇒ `pm_backup/restore` 是两个 `return BK_OK`；
`CONFIG_UART0_FLOW_CTRL` 未定义 ⇒ `uart_init_gpio` 的流控分支不编译；`CFG_CLI_DEBUG` 未定义；
`CONFIG_SPE=1` ⇒ `UART_CHECK_SECURE()`（`DEV_IS_SECURE()` 断言）保留。

**但实现必须 follow 配置，不按当前配置裁剪。** 权威里的 `#if CONFIG_UART_RX_DMA` / `CONFIG_UART_TX_DMA` /
`CONFIG_UART_PM_CB_SUPPORT` / `CONFIG_UART0_FLOW_CTRL` / `CFG_CLI_DEBUG` 分支要**原样保留**（连同 DMA 辅助函数、
`uart_id_to_dma_dev`、`uart_rx_dma_read_fifo_frame` 等），当前不编译只是当前配置的结果。
理由与 `bk7258_wdt.c` 保留 `CONFIG_INT_AON_WDT` 段、`bk7258_printf.c` 保留
`CONFIG_SYS_PRINT_DEV_MAILBOX` 分支一致：打开某个配置组合时不能撞到"缺实现"。
按当前配置裁剪等于把契约面从 57 个符号扩成"57 个符号 + 若干配置组合"，属于隐藏的契约退让。

**镜像里真正是活的只有 7 个符号**（`nuttx.map` 实测）：`bk_uart_driver_init`、`uart_id_to_pm_uart_id`、
`bk_uart_pm_backup/restore`（空壳）、`bk_uart_get_rx_gpio`、`bk_uart_get_ate_detect_gpio`、`uart_wait_tx_over`。
其余 50 个当前没有引用者（Armino CLI 已被我们的 compat shell 切掉输入，日志已改走 syslog）。
验收基线因此是：**替换后镜像里仍是同样的 7 个符号**。

**状态结构**（照抄）：
- `uart_driver_t { uart_hal_t hal; uint8_t id_init_bits; uint8_t id_sw_fifo_enable_bits;
  uint32_t pm_backup[UART_PM_BACKUP_REG_NUM]; uint8_t pm_bakeup_is_valid;
  bool rx_dma_enable; dma_id_t rx_dma_id; bool tx_dma_enable; dma_id_t tx_dma_id; }`
- `s_uart[SOC_UART_ID_NUM_PER_UNIT]`（`hal.hw` 分别指向 `SOC_UART0/1/2_REG_BASE`）
- `s_uart_rx_isr[] / s_uart_tx_isr[]`（`{callback, param}`）、`s_uart_rx_kfifo[]`、`s_uart_sema[]`（`{rx_blocked, rx_int_sema}`）
- `s_last_uart_rx_isr`（`take/recover` 用）

**分片与依赖**：

| 片 | 内容 | 依赖 |
| --- | --- | --- |
| 1 | `uart_clock_enable/disable`、`uart_interrupt_enable/disable`、`uart_init_gpio`、`uart_deinit_{tx,rx}_gpio` | `sys_drv_dev_clk_pwr_up`、`sys_drv_int_enable/disable`、`gpio_dev_map/unmap`、`bk_gpio_pull_up` |
| 2 | `uart_id_init_kfifo`、`uart_id_{init,deinit}_common`、`bk_uart_init/deinit`、`driver_init/deinit` | `kfifo_alloc/free`、`uart_statis_*`、`rtos_init/deinit_semaphore`、`bk_pm_sleep_register_cb` |
| 3 | 配置组：baud/data_bits/stop_bits/parity/enable_rx/tx/threshold/rx_timeout/flow_ctrl | `uart_hal_set_*`、`clk_get_uart_clk` |
| 4 | 读写组：`uart_write_byte/string/ready`、`bk_uart_write/read_bytes`、`read_byte(_ex)`、`get_length_in_buffer`、`wait_tx_over`、`is_tx_over` | `uart_hal_*`、`kfifo_*`、`rtos_get/set_semaphore` |
| 5 | 中断组：`register/take/recover_{rx,tx}_isr`、`uart_isr_common`、`uart0/1/2_isr`、`uart_get/clear_interrupt_status`、`isr_register_functions` | `bk_int_isr_register(INT_SRC_UARTn)`、`UART_STATIS_*` |
| 6 | 旁路：`enable/disable_sw_fifo`、`get_ate_detect_gpio`、`get_rx_gpio`、`write_byte_for_ate/for_fr`、`print_hex_dump`、`is_in_used`、`is_rx_dma_enabled`、`pm_backup/restore` 空壳、`uart_id_to_pm_uart_id` | 无 |

**必须照抄的行为**（容易漏）：
1. `uart_write_byte()` 先等 `fifo_write_ready` 再写；
2. `uart_write_string()` 在 `\n` 前补 `\r`（除非前一个已经是 `\r`）；
3. `uart_read_fifo_frame()` 在 kfifo 满时**仍然继续读空硬件 FIFO**（否则死循环），只是不 put；
4. `bk_uart_read_bytes()` 两条路：软件 FIFO 打开 → kfifo + 信号量等待（`rx_blocked` 门控、超时返回
   `BK_ERR_UART_RX_TIMEOUT`）；未打开 → 直接轮询 `uart_read_byte_ex()`；
5. `bk_uart_take_rx_isr()` 会先关软 FIFO、再换回调；`recover` 时若 `id == bk_get_printf_port()` 则不重新打开软 FIFO；
6. `bk_uart_wait_tx_over()` 用 `tx_fifo_count + 1` 与 `UART_CLOCK/(clk_div+1)` 估算等待微秒，再 `bk_delay_us()`；
7. `bk_uart_init()` 的顺序：关 TX 中断 → 清状态 → 注册 ISR → `uart_hal_init` → 中断使能 → 时钟/GPIO/kfifo/sema →
   选时钟源 → `uart_hal_init_uart` → `uart_hal_start_common`；并且**已初始化就直接返回 OK**；
8. `bk_uart_deinit()` 逆序：`uart_id_deinit_common`（停 HAL、复位配置、关中断/时钟、释放 kfifo 与信号量）。

**实现顺序**：1→2→3→4→5→6，每片写完单独编译（不接线），57 个齐了再一次性 exclude `uart_driver.c`。
