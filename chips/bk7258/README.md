# BK7258 芯片层

此目录承载所有与具体开发板无关的 BK7258 芯片层实现，并由仓库 manifest 映射到 `vendor/beken/chips/bk7258`。

这里承载与具体开发板无关的 BK7258 平台能力：启动入口、内存与堆初始化、时钟、中断、系统定时器、早期控制台、UART、GPIO、Mailbox 以及后续 PMU/ADC/OTP/Flash/DMA/Cache 驱动。实际寄存器、时钟、IRQ、内存布局和编译配置必须以 BK7258 TRM、可用 SDK/BSP 与上游适配规范为准。

CPU0 的启动、内存、链接与早期 UART 设计边界见 [BK7258 CPU0 Bring-up Contract](BK7258_CPU0_BRINGUP_CONTRACT.md)。该合同记录已确认的 Armino CP 产物和 DevKit 原理图证据，并明确尚不能写入 BSP 的未知项。

当前 CP/AP 启动、堆、时钟、IRQ、定时器、UART、GPIO 和 Mailbox 已有独立实现；Wi-Fi 需要的 PMU/模拟寄存器/OTP/ADC/Flash/DMA/Cache 能力按 [BK7258 Platform Peripheral Matrix](BK7258_PLATFORM_PERIPHERAL_MATRIX.md) 分阶段补齐。平台层不依赖 `chips/bk7258/wifi/`、WPA、`libwifi.a` 或闭源 RF 库。
