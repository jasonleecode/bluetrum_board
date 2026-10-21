# AB32VG1 开发板探索

## 关于本项目

这个项目围绕中科蓝讯 AB32VG1 开发板，分析官方闭源 SDK 中的 `libhal.a`、`libbtctrl.a` 和 Windows 配套工具，逐步恢复可阅读的驱动实现，并完善裸机示例和 Linux 开发流程。

官方资料并不完整，已收集的文档放在 `docs/`。逆向结论、实现和验证记录会一并保留，方便后续继续分析和上板核对。

## 项目目录

| 目录 | 内容 |
| --- | --- |
| [bare/](bare/README.md) | 裸机工程、外设驱动和应用示例 |
| [rtt/](rtt/README.md) | RT-Thread 工程与 BSP |
| [tools/linux/](tools/linux/README.md) | Linux DCF 打包、串口下载和检查工具 |
| [docs/](docs/README.md) | 芯片与开发板资料 |
| [docs/lib-analysis/](docs/lib-analysis/README.md) | SDK 库分析、驱动恢复和验证记录 |

## 当前进展

| 项目 | 进展 |
| --- | --- |
| 硬件 SPI1 | 已恢复 C 实现，通过原库差分测试和链接检查，待上板验证；见 [SPI/I2C 分析](docs/lib-analysis/SPI_I2C_RECOVERY.md) |
| 音频输入/输出 | 已恢复模拟输入与 SDADC 共 18 个函数，通过 3490 项原库差分测试；新增 48 kHz 双声道硬件 DAC 播放，DMA 采集 ABI 已修正，实机录放待完成，见 [音频分析](docs/lib-analysis/AUDIO_RECOVERY.md) |
| I2S 输出 | 从另一份 AB53XX SDK 恢复 SRC 主机输出，136 组差分通过；RT-Thread 接入 `sound0`，16 位双声道、44.1/48 kHz，待实测；见 [I2S 分析](docs/lib-analysis/I2S_RECOVERY.md) |
| 硬件 I2C | 当前 SDK 中尚未找到可恢复的硬件实现，仍需寄存器资料或其他固件证据；现有 BSP 使用软件 I2C |
| FM 收音机 | 已恢复模拟/数字通路 10 个函数，修正调谐参数，通过 332 项原库差分测试；RT-Thread 已接入手动调谐→DAC 播放、音量、停止及超时清理，搜台和实机收音待完成，见 [FM 分析](docs/lib-analysis/FM_RECOVERY.md) |
| 蓝牙 | 已恢复两个 HCI 消息对象及部分中断适配，完成原库差分和传输行为探测；仍依赖控制器/RF/ROM，板级配置与通信待验证，见 [蓝牙分析](docs/lib-analysis/BLUETOOTH_RECOVERY.md) |
| USB | 已恢复控制器、端点、EP0 和 HID 共 33 个函数，通过 1066 项原库差分测试；标准请求派发、板级接入与 Linux 枚举待完成，见 [USB 分析](docs/lib-analysis/USB_RECOVERY.md) |
| SD 卡 | 已根据寄存器手册和公开 BSP 实现原生 SD0 一位总线、SDSC/SDHC/SDXC 512 字节块读写；板级时钟/DMA 接入及实卡验证待完成，见 [SD 分析](docs/lib-analysis/SD_RECOVERY.md) |
| DCF 打包 | 已实现 Linux 工具，默认打包路径通过原版机器码逐字节对照 |
| 串口下载 | 已恢复握手、loader 上传、Flash 擦写、CRC 校验和重启，通过离线测试，待上板验证 |

Linux 工具的命令、支持范围和验证方法见 [使用说明](tools/linux/README.md)，打包格式和下载协议分别记录在 [XMAKER.md](tools/XMAKER.md) 与 [DOWNLOADER.md](tools/DOWNLOADER.md)。

## TODO

- [ ] **FM 收音机**：初始化与手动调谐已恢复，RT-Thread 已有 `fm0`/`fmrx` 命令与 DAC 播放；继续恢复搜台并完成实机验证。
- [ ] **蓝牙**：HCI 消息层与部分中断适配已恢复，广播/扫描/连接入口已定位；继续恢复状态机，补齐板级运行条件并完成通信示例。
- [ ] **USB 通信**：底层端点、EP0 状态机和 HID 辅助函数已恢复；补齐标准请求与描述符派发、板级初始化，验证 Linux 枚举及通信。现有描述符是单字节多媒体按键 HID，通用双向通信尚未实现。
- [ ] **SD 卡读写**：硬件 SD0 初始化、容量识别与单块读写已实现；完成板级 DMA 接入、实卡验证及文件系统集成。
- [ ] **音频接口**：模拟输入/SDADC 已恢复，硬件 DAC 播放与部分写入接口已实现；确认采集 DMA 格式/事件，完成实机录放、SD 连续供数；I2S 硬件输出已接入，继续恢复 RX/DMA 并验证同系列寄存器兼容性。

以上待办以裸机驱动恢复、示例和实机验证为目标。

## 开发板介绍

ab32vg1-prougen 是 中科蓝讯(Bluetrum) 推出的一款基于 RISC-V 内核的开发板，最高主频为 120Mhz，该开发板芯片为 AB32VG1。

开发板外观如下图所示：

![board](figures/board.png)

该开发板常用 **板载资源** 如下：

- MCU：AB32VG1，主频 120MHz，可超频至 192MHz，8Mbit FLASH ，192KB RAM。
- 常用外设
  - LED: RGB灯
  - 按键: 3 个, USER(s2,s3) and RESET(s1)
- 常用接口：USB 转串口、SD 卡接口

## 外设驱动状态

**并非所有外设都已在 `bare` 中实现，也不能全部标记为可用。** 以下区分裸机实现、RT-Thread BSP 源码与当前启用状态。“已有实现”只表示有对应代码；“部分实现”表示还有关键路径缺失；两者都不等于已通过实机验证。当前裸机工程已通过 GCC 编译和链接，完整启动、时钟、IRQ、真实 DMA 内存布局及外设实测仍待完成。

本表依据仓库当前 [rtconfig.h](rtt/bsp/rtconfig.h)、[BSP 构建规则](rtt/bsp/libraries/hal_drivers/SConscript)、[板级配置选项](rtt/bsp/board/Kconfig)及 `bare/drivers/` 核对。当前 RT-Thread 配置启用了 GPIO 和 UART0；源码中可选的其他驱动不代表默认已编入固件。RT-Thread 的通用 USB/I2C 等框架也不能代替芯片控制器驱动。

| 板载外设/接口 | `bare` 当前状态 | RT-Thread BSP 当前状态 |
| --- | --- | --- |
| USB 转串口 | 有 UART 库封装，待实测；桥接芯片由电脑侧驱动 | UART0 已启用；这是外置 USB-UART 桥，不是片上 USB Device |
| RGB LED / 用户按键 | 尚无通用 GPIO/按键驱动；RESET 是硬件复位键 | 有 GPIO 驱动并已启用；不代表已提供完整按键应用 |
| SD 卡槽 | 已实现原生 SD0 单块读写，待真实时钟/DMA 接入及实卡验证 | 有 SDIO 驱动和卡协议框架，当前未启用 |
| 红外遥控接收 | 尚未移植 | 有 `drv_irrx.c`，当前未启用；原表的“IRDA”不准确，不能据此认定支持完整 IrDA 协议 |
| 模拟音频输出 | 已实现片内 DAC 播放，默认平台钩子拒绝启动；待板级接入与实测 | 有 `sound0` 输出驱动，当前未启用 |
| 音频输入 | 已恢复 SDADC/MIC 底层并修正 DMA ABI；采样格式/事件待确认，AUX 不可用 | 不能由 `sound0` 输出驱动推断已支持录音 |

| 片上外设 | `bare` 当前状态 | RT-Thread BSP 源码/配置范围 |
| --- | --- | --- |
| GPIO | 尚无独立通用驱动；个别外设内部有专用引脚配置 | 有 `drv_gpio.c`，已启用 |
| UART | 有 `huart` 库封装，尚未独立恢复/实测；不等于全部 UART 通道已适配 | 有 UART0/1/2 配置选项，当前仅 UART0 启用 |
| SDIO/SD 控制器 | 已实现 SD0 一位 SD 卡模式；未实现四位、MMC、SDIO 功能卡 | 有 `drv_sdio.c`，当前未启用 |
| SARADC 电压采样 | 尚无裸机 SARADC 封装；`drv_adc` 是音频 SDADC | 有 `drv_adc.c` 的 SARADC 接入，当前未启用 |
| 音频 SDADC | 12 个底层函数及模拟输入恢复；完整 PCM 录音待完成 | 不属于上述 SARADC 驱动的支持范围 |
| 硬件 SPI1 | 已恢复并通过原库差分测试，待上板验证 | 已新增 `spi1` 硬件总线适配，分阶段收发/片选/超时；固定复位时序待验证，当前未启用 |
| 硬件 I2C | 尚未找到可恢复的硬件实现；现存 `drv_i2c` 是软件模拟，不满足本项目的硬件 I2C 目标 | 只有软件 I2C 适配，当前未启用 |
| RTC | 尚未移植 | 已修复时钟及时间回调；闹钟未实现，当前未启用 |
| WDT | 尚未移植独立驱动；其他底层函数中的喂狗操作不等于完整驱动 | 已修复秒单位、超时读回和停止；当前未启用 |
| FLASH | 有原库读写/擦除封装，尚未独立恢复，错误结果传播及实测待补 | 有片内 Flash/FAL 接入代码，当前未启用 |
| TIMER | 未实现，`drv_timer.c/.h` 为空文件 | 已修复溢出位/共享 IRQ/单次模式，当前未启用 |
| PWM | 尚未移植 | 有 `drv_pwm.c`，当前未启用；复用引脚/通道需按配置选择 |
| FM | 初始化、调谐及部分 DMA 已恢复；搜台、完整收音链路待完成 | 已新增 `fm0`、`fmrx` 命令及 DAC 播放/互斥/超时处理；可选、未实测 |
| 蓝牙 | HCI 支持层部分恢复；控制器/RF 仍依赖原库，平台默认拒绝启动 | 有 NimBLE 配置入口，当前未启用；入口存在不代表链路已验证 |
| I2S/IIS | 已恢复另一份 AB53XX SDK 的 I2S SRC 输出子集，136 组差分通过；RX/DMA 未恢复 | `BSP_USING_I2S` 将 PCM 送到硬件 I2S；16 位双声道、44.1/48 kHz；实验性、默认关闭、未实测 |
| USB Device | 底层端点、EP0、HID 部分恢复；请求派发、枚举及通用通信尚未完成 | 未见已接入本芯片的 Device 控制器驱动 |
| USB Host | 只有模式切换入口，主机枚举与传输驱动未实现 | 未见已接入本芯片的 Host 控制器驱动 |

芯片硬件具备 USB Host/Device 能力，但当前软件尚未完成两种模式的可用接入；具体恢复范围见上方各专题文档。后续应逐项完成“实现 → 板级接入 → 实机验证”，再将对应项标记为已验证支持。

RT-Thread 的驱动修复、配置限制和构建验证见 [BSP 维护说明](rtt/README.md#linux-构建与驱动维护)。
