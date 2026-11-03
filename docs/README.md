# 项目文档

## 综合 RTT 工程

RT-Thread 专题文档集中在 [docs/rtt/](rtt/)，工程说明保留在 [rtt/README.md](../rtt/README.md)，源码位于仓库的 [rtt/](../rtt/) 目录。

| 文档 | 内容 |
| --- | --- |
| [工程说明](../rtt/README.md) | 构建、烧录、串口使用、功能进展和配置限制 |
| [内存预算](rtt/MEMORY.md) | SRAM 布局、堆占用及模块共存限制 |
| [NimBLE](rtt/NIMBLE.md) | 蓝牙协议栈、BLE/GATT 测试与 iPhone 扫描 |
| [MP3 / AUDEC](rtt/MP3.md) | 硬件解码接口、双声道及验证记录 |
| [USB](rtt/USB.md) | CDC 软件适配、V2.0 接口与接线 |
| [TF 卡](rtt/TF.md) | SD0、块设备和 FAT 文件系统 |
| [红外接收](rtt/IR.md) | 接线、NEC 键码与接收驱动 |
| [按键](rtt/KEYS.md) | S2/S3/S4、消抖和事件接口 |
| [PWM](rtt/PWM.md) | PWM 驱动与 S2 呼吸灯 |
| [RTC](rtt/RTC.md) | 秒计数、校时和走时验证 |
| [电源管理](rtt/POWER.md) | 浅休眠、唤醒和软关机 |
| [SPI / I2C](rtt/SPI_I2C.md) | 硬件 SPI 接入、测试及 I2C 证据缺口 |

## 芯片资料与驱动恢复

- [厂商资料](ab32vg1_doc/README.md)：芯片手册、开发板原理图和示例。
- [SDK 库分析](lib-analysis/README.md)：驱动恢复、差分测试和实机记录。
- [Linux 构建与下载工具](../tools/linux/README.md)：DCF 打包、CH340 烧录和串口工具。

`bluetrum_sdk/` 和 `downloader/` 保留已收集的厂商 SDK 与原版下载工具。
