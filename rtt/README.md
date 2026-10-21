# AB32VG1 骄龙开发板 BSP 说明

## 简介

主要内容如下：

- 开发板资源介绍
- BSP 快速上手
- 进阶使用方法

通过阅读快速上手章节开发者可以快速地上手该 BSP，将 RT-Thread 运行在开发板上。在进阶使用指南章节，将会介绍更多高级功能，帮助开发者利用 RT-Thread 驱动更多板载资源。

## 注意事项

芯片有部分不开源的代码是以静态库提供的，静态库在软件包中，默认已勾选，直接运行 `pkgs --update` 即可

波特率默认为 1.5M，需要使用 [Downloader](https://github.com/BLUETRUM/Downloader) 下载 `.dcf` 到芯片，需要编译后自动下载，需要在 `Downloader` 中的下载的下拉窗中选择 `自动`；目前暂时屏蔽 uart1 打印

使用 `romfs` 时，需要自己生成 `romfs.c` 进行替换，操作参考[使用 RomFS](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/tutorial/qemu-network/filesystems/filesystems?id=%e4%bd%bf%e7%94%a8-romfs)

编译报错的时候，如果出现重复定义的报错，可能需要在 `cconfig.h` 中手动添加以下配置

``` c
#define HAVE_SIGEVENT 1
#define HAVE_SIGINFO 1
#define HAVE_SIGVAL 1
```

所有在中断中使用的函数或数据需要放在 RAM 中，否则会导致系统运行报错。具体做法可以参考下面

``` c
rt_section(".irq.example.str")
static const char example_info[] = "example 0x%x";

rt_section(".irq.example")
void example_isr(void)
{
    rt_kprintf(example_info, 11);
    ...
}
```



## 使用说明

使用说明分为如下两个章节：

- 快速上手

    本章节是为刚接触 RT-Thread 的新手准备的使用说明，遵循简单的步骤即可将 RT-Thread 操作系统运行在该开发板上，看到实验效果 。

- 进阶使用

    本章节是为需要在 RT-Thread 操作系统上使用更多开发板资源的开发者准备的。通过使用 ENV 工具对 BSP 进行配置，可以开启更多板载资源，实现更多高级功能。


### 快速上手

本 BSP 为开发者提供 GCC 开发环境。下面介绍如何将系统运行起来。

#### 硬件连接

使用数据线连接开发板到 PC，打开电源开关。

#### 编译下载

通过 `RT-Thread Studio` 或者 `scons` 编译得到 `.dcf` 固件，通过 `Downloader` 进行下载

#### 运行结果

下载程序成功之后，系统会自动运行，观察开发板上 LED 的运行效果，红色 LED 会周期性闪烁。

连接开发板对应串口到 PC , 在终端工具里打开相应的串口（115200-8-1-N），复位设备后，可以看到 RT-Thread 的输出信息:

```bash
 \ | /
- RT -     Thread Operating System
 / | \     4.0.3 build Dec  9 2020
 2006 - 2020 Copyright by rt-thread team
msh >
```
### 进阶使用

此 BSP 默认只开启了 GPIO 和 串口0 的功能，如果需使用 SD 卡、Flash 等更多高级功能，需要利用 ENV 工具对BSP 进行配置，步骤如下：

1. 在 bsp 下打开 env 工具。

2. 输入`menuconfig`命令配置工程，配置好之后保存退出。

3. 输入`pkgs --update`命令更新软件包。

4. 输入`scons` 命令重新编译工程。

## Linux 构建与驱动维护

本仓库默认自动选择根目录 `.toolchains/` 下的 SiFive GCC 10.1.0，
并链接已有的 `bare/lib/libhal.a`。在已安装 SCons 的 Linux 环境执行：

```sh
cd rtt/bsp
scons -j4
```

可用 `RTT_EXEC_PATH=/编译器/bin RTT_CC_PREFIX=riscv64-unknown-elf- scons -j4`
覆盖工具链。生成 `rtthread.elf`、`rtthread.bin`；Linux DCF 打包/下载另见
[工具说明](../tools/linux/README.md)。构建通过不代表已在开发板启动或完成外设实测。

当前 `rtconfig.h` 默认仍只启用 GPIO 和 UART0。可选驱动通过 BSP 的
`Hardware Drivers Config` 菜单配置，不能把所有选项同时开启：
TIMER3/4/5 与对应 PWM 共用计数器，TIMER3 与红外共用中断；SPI 的组 2
与 UART0、组 3 与 SD/UART2、组 1 与 UART1 有引脚冲突。

| 功能 / 配置项 | 本次完善内容及边界 |
| --- | --- |
| GPIO / `RT_USING_PIN` | 修正不存在的 PG 端口、非法引脚名和编号越界；LED/按键通过 PIN API 使用 |
| SPI1 / `BSP_USING_SPI1` | 新增 `spi1` 硬件总线，支持主机 8 位 MSB、分阶段收发、GPIO 片选、向上取整分频和回绕安全的超时；不使用软件 SPI |
| SARADC / `BSP_USING_ADC0` | 适配当前 RT-Thread 回调签名，校验 0..15 通道，传递转换超时，不再返回陈旧采样值 |
| TIMER / `BSP_USING_TIM` | 区分 TIMER1/2 bit9 与 TIMER3/4/5 bit16 的溢出/清除位；共享 IRQ 只处理真实 pending；支持单次模式，检查计数溢出，开启时钟，并将 IRQ 调用的停止操作及函数表放入 RAM |
| RTC / `BSP_USING_ONCHIP_RTC` | 修复时钟选择赋值及 `time_t` 回调，拒绝负值和超过 32 位计数范围的时间；未完成的闹钟/秒中断不再作为可用操作暴露 |
| WDT / `BSP_USING_WDT` | SET/GET_TIMEOUT 使用 RT-Thread 的秒单位，输入 1..16 秒向上选择 1/2/4/8/16 档，实际硬件约为 1.024/2.048/4.096/8.192/16.384 秒；支持停止，拒绝空参数。旧 `AB32_WDT_TIMEOUT_*` 是寄存器编码，不能作为秒值传入 |
| SD / `BSP_USING_SDCARD` | 原生 SD0 一位总线；修复命令/数据事件等待、CRC/超时传递、时钟分频、缓存边界、写入长度和 host 初始化竞态；单请求上限 1024 字节（两个 512 字节块），已编译 SD＋FAT 配置 |
| 音频播放 / `BSP_USING_AUDIO` | 修正 48 kHz PLL，接受 44.1/48 kHz、16 位、单/双声道配置；拒绝未支持格式与录音，修正错误返回、分配失败清理、空 replay 访问及停止 DAC；缓冲区等待有超时。实际录放未验证；I2S 可选输出另见下项 |
| FM / `BSP_USING_FMRX` | 直接编译 bare 已恢复的模拟/数字代码；`fm0` 和 `fmrx` 命令接入手动调谐、音量、DAC 播放、周期同步、停止、PLL 超时及音频互斥；尚无搜台/RDS/实机收音验证 |
| I2S / `BSP_USING_I2S` | 新增同系列硬件 SRC 输出子集，接 `sound0`；主机、标准 I2S、16 位双声道、MCLK=256fs、44.1/48 kHz；未接收/DMA，兼容性仍需上板验证 |
| Flash / `BSP_USING_ON_CHIP_FLASH` | 自动选择 FAL；校验 1 MiB 地址范围和写擦对齐，读写分为 256 字节调用，返回实际请求字节数，避免把原库 CRC16 当成读取长度。仍依赖原库，写擦 API 没有错误返回，应用应读回校验；分区布局须与最终打包固件一致 |
| PWM、红外 | 分别通过可选配置编译；红外是遥控接收，不是完整 IrDA。尚无波形/遥控器实测 |

SPI1 的 `BSP_SPI1_MAPPING` 取值为 1..5，默认 4：MISO=PE5、SCK=PE6、MOSI=PE7；
片选由应用用 `ab32_spi1_attach()`（`drv_spi.h`）挂接外设时指定；该入口预置不选中电平、检查时钟及引脚冲突，并在配置失败时撤销挂接。
使用 `rt_spi_send()`、`rt_spi_recv()` 或 `rt_spi_send_then_recv()`。
同时提供发送与接收缓冲的全双工请求返回 `-RT_ENOSYS`，不会用两次传输冒充一次全双工。
`RT_SPI_MODE_0 | RT_SPI_MSB` 暂作为原 SDK 复位时序的 API 配置入口，**不代表已确认
CPOL/CPHA 波形**；其他模式、LSB、从机、3-wire 被拒绝。上板前需用示波器确认时序。
分频暂保守限制 1..256，48 MHz 系统时钟下请求频率低于 187.5 kHz 会被拒绝。

### SPI、FM 与 I2S 接入

用 `scons --menuconfig` 在 BSP 的 `Hardware Drivers Config` 中启用所需功能，
再执行 `scons -j4`。这些选项默认关闭，当前默认固件仍只有 GPIO/UART0。
同时使用时建议 SPI mapping 4（PE5/6/7）、I2S mapping 1（PE0..4）；
启用 I2S PE 组会停用 `main.c` 的 PE1 LED 闪烁。板载 LED 负载、外部 codec
及其它 PWM/UART 占用仍需检查，不能以编译通过代替电气验证。

SPI 使用示例（`sensor` 必须有静态或更长生命周期，CS 根据实际接线选择）：

```c
#include "drv_spi.h"
static struct rt_spi_device sensor;
/* 仅示例：PA.0 在 I2S PE 组下不冲突；实际 CS 由接线决定。 */
rt_err_t err = ab32_spi1_attach(&sensor, "sensor", rt_pin_get("PA.0"), 1000000);
if (err == RT_EOK) {
    /* 根据外设协议使用 rt_spi_send / recv / send_then_recv。 */
}
```

FM 启用 `BSP_USING_FMRX` 后，在 FinSH 中使用：

```text
fmrx start 101100
fmrx tune 87500
fmrx volume 30
fmrx status
fmrx stop
```

频率单位 kHz，范围 76000..108000，步进 10。音频走片内 DAC/耳机口；
使用合适的 FM 天线，实机接收效果尚未验证。应用也可通过 `fm0` 的
`rt_device_control()` 调用 [drv_fmrx.h](bsp/board/ports/audio/drv_fmrx.h) 中的命令。
启动前须关闭已打开的 `sound0`；FM 运行时 PCM open/write/control 会返回忙，
避免混用音源。调谐 PLL 100 ms 超时后关闭 RF/DMA，释放 DAC；之后可重新启动。
FM 不申请 PCM 播放队列，工作线程每 100 ms 调用恢复的同步函数。
自动搜台、RSSI、RDS、原始采集 DMA 的完成事件尚未恢复。

I2S 启用 `BSP_USING_I2S` 后，PCM 仍写 `sound0`，与片内 DAC 同时输出。
仅接受 44.1/48 kHz、16 位、双声道；PE 组 DO=PE1、BCLK=PE2、LRCLK=PE3、
MCLK=PE4，PA 组对应 PA1..4。外部 codec 需自行配置为兼容的从机。
FM 的 38 kHz 路径不输出到 I2S，停止 FM 后可恢复 PCM/I2S 播放。
新恢复代码、来源、差分记录及限制见 [I2S 分析](../docs/lib-analysis/I2S_RECOVERY.md)。

硬件 I2C 仍缺少可确认的控制器定义或实现对象；新找到的 AB53XX SDK 也只提供
GPIO 软件 I2C，因此没有将其当成硬件驱动接入。USB 和蓝牙按当前优先级暂缓。

### 离线验证

在仓库根目录执行：

```sh
python3 tools/rtt/test_drivers.py
python3 tools/rtt/check_builds.py default peripherals audio pwm ir sd flash spi_fm spi_fm_i2s i2s_pa
```

第一条用真实 RT-Thread 头文件与模拟寄存器运行 BSP 回调，覆盖 SPI 超时/tick 回绕/片选、
ADC 错误、TIMER 共享中断和溢出、WDT 秒单位、RTC 时间范围、音频参数/PLL/停止、
Flash 分页/返回值，以及 SD 命令和数据完成事件、超时/CRC/缓存边界。
新增 FM 状态机、音频入口互斥、超时释放/重启、I2S 输出启停与格式限制检查。
它不验证硬件时序、DMA 电气行为或真实卡片协议。

第二条在 `/tmp/ab32-rtt-build-*` 的独立源码副本中执行指定配置的 GCC 完整构建，
保留各配置 ELF、map 和日志，并检查 FM 未链接回原库 FM 对象，不修改工作目录的 `rtconfig.h`，也不烧写设备。
`peripherals` 包含 SPI1、SARADC、RTC、WDT 和 TIMER1..5；其他冲突外设分别构建。

## 维护人信息

- [greedyhao](https://github.com/greedyhao)
