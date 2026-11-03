# AB32VG1 骄龙开发板 BSP 说明

## 简介

本目录是开发板的统一综合项目，FM、蓝牙、音频及后续外设均作为模块集成到
同一 RT-Thread 工程和固件。共享资源由项目统一管理，并分别验证模块功能及共存行为。
当前已接入 GPIO、UART0、DAC 音频、FM 和恢复源码的蓝牙控制器（见 [恢复记录](../docs/lib-analysis/BLUETOOTH_RECOVERY.md)）。综合 RTT 不链接 `libbtctrl.a`。NimBLE Host 已通过 `rtt/nimble` 子模块接入，BLE 连接、ACL 分片、GATT 读写和通知已在实板验证；构建、命令与使用边界见 [NimBLE 接入说明](NIMBLE.md)。
原生 USB CDC 已完成软件适配、离线测试和编译；V2.0 的原生数据线接 Host 用 USB-A，实机枚举及收发待 Device 接口及供电适配后验证；见 [USB 接入说明](USB.md)。
TF 的原生 SD0、块设备 tf0 和 FAT12/16/32 已完成软件集成及离线测试，实卡验证待卡到位后进行；见 [TF 接入说明](TF.md)。
板载 IRDA 接口已接入 ir0 和 irrx 命令，扫地机器人遥控器的 NEC 四向键、长按重复及释放已实测通过；见 [红外接入说明](IR.md)。
S2/PF1、S3/PF0、S4/PA2 三个功能键默认接入 keys0，支持消抖和长按，见 [按键说明](KEYS.md)。
原生 RTC 秒计数读写已修复并默认注册 rtc，使用内部 RC 时钟；已烧录验证读写及约 60 秒连续走时，见 [RTC 说明](RTC.md)。
电源管理驱动默认注册 power0，提供 pmctl 状态、PWKEY/VUSB 配置、保留时钟的浅休眠及唤醒复位式软关机；
已烧录验证连续 10 次 RTC 唤醒、tick 补偿及唤醒后的 S2 呼吸灯；软关机/功耗待测，见 [POWER.md](POWER.md)。
AUDEC 的 MP3 硬件后端已整理为可选 `audec0` 驱动，支持独占会话、同步 PCM 输出和超时恢复；
当前蓝牙布局与其工作 RAM 冲突，因此默认关闭，完整 MP3 软件前端尚未接入，见 [MP3.md](MP3.md)。

主要内容如下：

- 开发板资源介绍
- BSP 快速上手
- 进阶使用方法

通过阅读快速上手章节开发者可以快速地上手该 BSP，将 RT-Thread 运行在开发板上。在进阶使用指南章节，将会介绍更多高级功能，帮助开发者利用 RT-Thread 驱动更多板载资源。

## 注意事项

芯片有部分不开源的代码是以静态库提供的，静态库在软件包中，默认已勾选，直接运行 `pkgs --update` 即可

本仓库默认控制台波特率为 115200（8N1），可使用 Linux 下载器或 [Downloader](https://github.com/BLUETRUM/Downloader) 下载 `.dcf` 到芯片，需要编译后自动下载，需要在 `Downloader` 中的下载的下拉窗中选择 `自动`；目前暂时屏蔽 uart1 打印

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

下载程序成功之后，系统会自动运行。当前默认红灯上电熄灭，按 S2 开启周期性呼吸、再按停止，见 [PWM 说明](PWM.md)。

连接开发板对应串口到 PC , 在终端工具里打开相应的串口（115200-8-1-N），复位设备后，可以看到 RT-Thread 的输出信息:

```bash
 \ | /
- RT -     Thread Operating System
 / | \     4.0.3 build Dec  9 2020
 2006 - 2020 Copyright by rt-thread team
msh >
```
### 进阶使用

当前配置开启了 GPIO、三个按键、串口0、DAC 音频、FM、恢复源码的蓝牙控制器、NimBLE Host、实验性 USB CDC、TF/FAT、红外接收和 RTC，如果需使用 Flash 等更多高级功能，需要利用 ENV 工具对BSP 进行配置，步骤如下：

1. 在 bsp 下打开 env 工具。

2. 输入`menuconfig`命令配置工程，配置好之后保存退出。

3. 输入`pkgs --update`命令更新软件包。

4. 输入`scons` 命令重新编译工程。

## Linux 构建与驱动维护

本仓库默认自动选择根目录 `.toolchains/` 下的 SiFive GCC 10.1.0，
并链接已有的 `bare/lib/libhal.a`。在已安装 SCons 的 Linux 环境执行：

```sh
git submodule update --init rtt/nimble
cd rtt/bsp
scons -j4
```

可用 `RTT_EXEC_PATH=/编译器/bin RTT_CC_PREFIX=riscv64-unknown-elf- scons -j4`
覆盖工具链。生成 `rtthread.elf`、`rtthread.bin`。在仓库根目录打包并下载：

```sh
tools/linux/convert rtt/bsp/header.bin rtt/bsp/rtthread.bin rtt/bsp/rtthread.dcf
tools/linux/downloader flash rtt/bsp/rtthread.dcf --port /dev/ttyUSB0 --connect-timeout 120 --timeout 5
```

下载器等待连接时按 RESET（S1）。同一 CH340 串口用于运行期控制台，参数为
**115200、8N1、无流控**；发送命令后以回车结束。单线连接有硬件回显，
当前 msh 已适配 UART0 的硬件回显，普通字母不会再被软件重复发送。
终端应关闭本地回显：minicom 中按 `Ctrl-A` 后按 `E`，确认 Local echo 为 off。
使用 VT100/VT102 兼容终端，脚本逐字符发送时建议间隔 20 ms，留出处理时间。
可执行 `version`、`ps`、`free`、`list device`。

#### 单线串口与 msh 回显

板载 CH340 的 TX/RX 在单线下载口相连，电脑发出的字符会直接回到电脑；原 msh 默认
又发送一次字符，因此即使关闭 minicom 本地回显，也会看到 `vveerrssiioonn`。
当前默认配置开启 `FINSH_USING_HARDWARE_ECHO`，指定
`FINSH_HARDWARE_ECHO_DEVICE="uart0"`，抑制行末普通字符的软件回显。
保留回车换行，在退格、方向键、历史和行内编辑前通过 VT100 保存/恢复光标及重画当前行，
避免硬件回显已经移动光标后再移动一次。其他串口仍使用普通 msh 回显。
原始串口日志会包含 `ESC 7/8` 等终端控制字节，这是光标处理，不是重复输入。

不能用 `FINSH_ECHO_DISABLE_DEFAULT` 代替此适配：它也会关闭换行和部分编辑输出。
本模式支持基本 ASCII 单行编辑，不启用认证、扩展 Home/End/Insert/Delete 或 Ctrl 单词操作；
切换到不同接线或控制台时需相应调整配置。USB CDC 当前仍是二进制回传，不是 msh 控制台。

修复版已烧录并通过 Flash CRC 校验。真实 FinSH 的主机终端模型通过；实板逐字节确认
每字母仅一次回显，并验证 BS/DEL 退格、行内插入、左右键、历史和 Tab 补全。
复测命令如下（实板测试前先退出 minicom）：

```sh
python3 tools/rtt/test_msh_echo.py
python3 tools/rtt/test_msh_uart.py --port /dev/ttyUSB0 --output /tmp/msh-echo.json
```

结果见 [msh 回显验证](../docs/lib-analysis/generated/recovery/msh-echo-hardware.json)。

### 2026-11-02 上板验证

已在当前 CH340 / CRWN 开发板上完成干净编译、Flash 写入及逐块 CRC 校验，
并验证 RT-Thread **5.2.2** 启动、`Hello, world` 和 `msh >` 提示符。
`ps` 可见 main、tshell、drv、gpioirq、timer、tidle0 六个线程；
`free` 显示总堆 53160 字节、已用 9824 字节、可用 43336 字节。
主线程通过 PIN API 每 500 ms 翻转 PE1 红灯，沿用 P1 的 3–4 脚跳线。
原始串口验证输出保存在本地 `bsp/uart-verify.log`。
随后连续执行 40 条版本、线程、堆内存、设备查询，全部成功返回 shell 提示符；
收取 11600 字节，测试前后堆使用量一致。

启动修复包括：初始化启动栈并清除完整 BSS；纠正线程 tp 寄存器、上下文
保存大小及首次切换；将当前内核的 scheduler/klibc 放入 RAM；修正 UART
引脚复用清位并在 ROM/cache 调用前准备串口时钟和引脚；时钟切换前停用
继承的 UART；缩短超出 RT_NAME_MAX 的互斥锁和 GPIO 线程/队列名称。
SCons 默认目标为完整固件，并跟踪链接脚本变化，避免继续使用旧链接结果。

上述记录对应 FM 启用前的 GPIO/UART0 配置；FM 调试记录见下文。
更多下载器说明见 [工具说明](../tools/linux/README.md)。

当前 `rtconfig.h` 默认启用 GPIO、三个按键、UART0、DAC 音频、FM、恢复源码的蓝牙控制器与 NimBLE Host，以及实验性 USB CDC、TF/FAT、红外和 RTC。可选驱动通过 BSP 的
`Hardware Drivers Config` 菜单配置，不能把所有选项同时开启：
TIMER3/4/5 与对应 PWM 共用计数器，TIMER3 与红外共用中断；SPI 的组 2
与 UART0、组 3 与 SD/UART2、组 1 与 UART1 有引脚冲突。

| 功能 / 配置项 | 本次完善内容及边界 |
| --- | --- |
| GPIO / `RT_USING_PIN` | 修正非法引脚、复用恢复、SET/CLR、共享中断回调及清 pending；通用边沿回调通过 BSP_USING_PIN_IRQ 可选开启，默认关闭 |
| 三个按键 / `BSP_USING_KEYS` | 默认 keys0，主线程 10 ms 扫描，30 ms 消抖、800 ms 长按；三个键的按下/松开及一次长按已实测，见 [KEYS.md](KEYS.md) |
| SPI1 / `BSP_USING_SPI1` | 新增 `spi1` 硬件总线，支持主机 8 位 MSB、分阶段收发、GPIO 片选、向上取整分频和回绕安全的超时；不使用软件 SPI |
| SARADC / `BSP_USING_ADC0` | 适配当前 RT-Thread 回调签名，校验 0..15 通道，传递转换超时，不再返回陈旧采样值 |
| TIMER / `BSP_USING_TIM` | 区分 TIMER1/2 bit9 与 TIMER3/4/5 bit16 的溢出/清除位；共享 IRQ 只处理真实 pending；支持单次模式，检查计数溢出，开启时钟，并将 IRQ 调用的停止操作及函数表放入 RAM |
| RTC / `BSP_USING_ONCHIP_RTC` | 默认启用；修正时钟选择、寄存器返回、无限等待和中断恢复；保留计数，首次校时用 rtcctl set；软件验证及实板读写、范围拒绝、约 60 秒走时通过；当前 time_t 支持至 2038 年，闹钟及秒中断设备接口未实现；电源驱动已验证 RTC 秒唤醒，见 [RTC.md](RTC.md) |
| WDT / `BSP_USING_WDT` | SET/GET_TIMEOUT 使用 RT-Thread 的秒单位，输入 1..16 秒向上选择 1/2/4/8/16 档，实际硬件约为 1.024/2.048/4.096/8.192/16.384 秒；支持停止，拒绝空参数。旧 `AB32_WDT_TIMEOUT_*` 是寄存器编码，不能作为秒值传入 |
| TF / `BSP_USING_TFCARD` | 当前默认：复用原生 SD0，静态 DMA 中转区、串行块 I/O、显式探测和 FAT 挂载；无卡启动不探测，离线测试通过，实卡待验证，见 [TF.md](TF.md) |
| 旧 SD / `BSP_USING_SDCARD` | 原生 SD0 一位总线；修复命令/数据事件等待、CRC/超时传递、时钟分频、缓存边界、写入长度和 host 初始化竞态；单请求上限 1024 字节（两个 512 字节块），已编译 SD＋FAT 配置 |
| 音频播放 / `BSP_USING_AUDIO` | 修正 48 kHz PLL，接受 44.1/48 kHz、16 位、单/双声道配置；拒绝未支持格式与录音，修正错误返回、分配失败清理、空 replay 访问及停止 DAC；缓冲区等待有超时。DAC 测试音与 `pcmcheck` 静音播放已实测，完整音频播放场景及录音未验证；I2S 可选输出另见下项 |
| FM / `BSP_USING_FMRX` | 直接编译 bare 已恢复的模拟/数字代码；`fm0` 和 `fmrx` 命令接入手动调谐、音量、DAC 播放、周期同步、停止、PLL 超时及音频互斥；控制与 DAC 测试音已实测，广播仍只有杂音，搜台/RDS 未完成 |
| I2S / `BSP_USING_I2S` | 新增同系列硬件 SRC 输出子集，接 `sound0`；主机、标准 I2S、16 位双声道、MCLK=256fs、44.1/48 kHz；未接收/DMA，兼容性仍需上板验证 |
| Flash / `BSP_USING_ON_CHIP_FLASH` | 自动选择 FAL；校验 1 MiB 地址范围和写擦对齐，读写分为 256 字节调用，返回实际请求字节数，避免把原库 CRC16 当成读取长度。仍依赖原库，写擦 API 没有错误返回，应用应读回校验；分区布局须与最终打包固件一致 |
| 红外 / `BSP_USING_IRRX` | 默认编入原生 NEC/扩展 NEC 接收、8 项静态事件队列及 irrx 命令；显式启停，无新线程；NEC 遥控器四向键实测通过，释放宽限 400 ms，见 [IR.md](IR.md) |
| PWM | 默认启用 Timer5 PWM0/PE1，由 S2 切换呼吸灯；驱动修复及边界测试见 [PWM.md](PWM.md) |
| 电源管理 / `BSP_USING_POWER` | 默认编入 power0/pmctl，复用 RTC 锁和有界传输；状态、PWKEY 输入/滤波、WK/VUSB 唤醒配置、保留时钟浅休眠及复位式关机已做软件验证；已实测连续 10 次 RTC 秒唤醒及 tick 补偿，off/功耗待测，见 [POWER.md](POWER.md) |

SPI1 的 `BSP_SPI1_MAPPING` 取值为 1..5，默认 4：MISO=PE5、SCK=PE6、MOSI=PE7；
片选由应用用 `ab32_spi1_attach()`（`drv_spi.h`）挂接外设时指定；该入口预置不选中电平、检查时钟及引脚冲突，并在配置失败时撤销挂接。
使用 `rt_spi_send()`、`rt_spi_recv()` 或 `rt_spi_send_then_recv()`。
同时提供发送与接收缓冲的全双工请求返回 `-RT_ENOSYS`，不会用两次传输冒充一次全双工。
`RT_SPI_MODE_0 | RT_SPI_MSB` 暂作为原 SDK 复位时序的 API 配置入口，**不代表已确认
CPOL/CPHA 波形**；其他模式、LSB、从机、3-wire 被拒绝。上板前需用示波器确认时序。
分频暂保守限制 1..256，48 MHz 系统时钟下请求频率低于 187.5 kHz 会被拒绝。

### SPI、FM 与 I2S 接入

用 `scons --menuconfig` 在 BSP 的 `Hardware Drivers Config` 中启用所需功能，
再执行 `scons -j4`。当前默认启用音频和 FM，SPI 与 I2S 仍关闭。
同时使用时建议 SPI mapping 4（PE5/6/7）、I2S mapping 1（PE0..4）；
启用 I2S PE 组须关闭 `BSP_USING_LED_BREATH` 和 `BSP_USING_T5_PWM0`，同时停用旧 PE1 LED 闪烁。板载 LED 负载、外部 codec
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
fmrx volume 20
fmrx start 97400
fmrx tune 87500
fmrx status
fmrx rssi
fmrx diag
fmrx stop
```

频率单位 kHz，范围 76000..108000，步进 10。音频走片内 DAC/耳机口；
使用合适的 FM 天线，实机接收效果尚未验证。应用也可通过 `fm0` 的
`rt_device_control()` 调用 [drv_fmrx.h](bsp/board/ports/audio/drv_fmrx.h) 中的命令。
启动前须关闭已打开的 `sound0`；FM 运行时 PCM open/write/control 会返回忙，
避免混用音源。调谐 PLL 100 ms 超时后关闭 RF/DMA，释放 DAC；之后可重新启动。
FM 不申请 PCM 播放队列，工作线程每 100 ms 调用恢复的同步函数。
自动搜台、RSSI 标定、RDS、原始采集 DMA 的完成事件尚未恢复。

### FM 实机调试记录（2026-11-02）

**尚未完成广播接收验证。** 已编译并经 Flash CRC 校验下载，`fmrx start/tune/stop`
和音量控制可以运行；用户确认 `fmrx tone` 的 10 秒低音量测试音清晰、稳定。
恢复 97.4 MHz 后仍只有沙沙声，不能将命令返回成功视为收到电台。

- 已启用 DAC 时钟，修正音频 PLL 稳定后再选择时钟族的顺序；FM 固定使用
  48 kHz 时钟族的 38 kHz 输出档，释放 DAC 时恢复原 PCM 设置。
- 单独启用 FM 时使用 `bsp/link_fm_ram.lds` 将代码和数据放入 SRAM，避免调试期间
  Flash 缓存重入调度器。保留 ROM 区与 4 KiB 中断栈，链接时要求至少 16 KiB 堆。
  该次 FM 调试的运行时总堆为 51656 字节，可用 28784 字节。
  当前同时启用蓝牙时改用 `bsp/link_bt_ram.lds`；内存预算见下方蓝牙记录。
- `fmrx diag` 读取 PLL、DAC 和 FIFO 状态；`fmrx rssi` 在 100 ms 超时内读取
  接收原始值，不提供已校准的 dB 单位，也不据此宣称检测到电台。
- `fmrx tone` 停止 FM，播放 10 秒低音量测试音后释放 DAC，并保持 FM 关闭；
  要恢复广播，执行 `fmrx volume 20`、`fmrx start 97400`。
- 87.5–108 MHz、100 kHz 步进的扫描未找到明确的信号峰。两份 SDK 中
  `0x414[10:9]` 的不同预设及 RF 电容 0–31 档对比未见明确改善；临时控制命令已删除。
- 用户确认四段耳机插头已插到底、耳机线已展开；暂时无法移至窗边或更换耳机。
  接收环境、耳机天线接线、板上射频路径与未覆盖的初始化要求仍未区分。
  后续应使用已知可接收的环境/天线，或测量射频路径，不应继续用“驱动无错误”代替收音验收。

最终版本通过默认、audio、SPI＋FM＋I2S 三种完整构建和四组主机驱动测试；
上板重复启停、调谐后堆使用量不变。该次 FM 测试结束时保持 97.4 MHz、音量 0。
原始上板输出保存在本地 `bsp/fm-verify.log`（不提交）。

### 换耳机后继续测试

当前 FM 调试代码已纳入 BSP 默认配置，重启后注册 `fm0`，不会自动播放。
如果本次上电已运行 `ble start` 或 `btctl test`，请先复位；停止广播不会释放蓝牙射频。
插入 CTIA 标准有线耳机并展开耳机线，使用烧录所用串口（115200、8N1）执行：

```text
fmrx stop
fmrx tone
fmrx volume 20
fmrx start 97400
fmrx status
fmrx rssi
fmrx diag
```

`fmrx tone` 需要等待约 10 秒，听到清晰测试音后再执行后续命令。
最后以能辨认广播人声或音乐作为接收验收依据；若仍是杂音，保留 `rssi`、`diag`
输出用于对照，执行 `fmrx stop` 结束测试。

### I2S 使用

I2S 启用 `BSP_USING_I2S` 后，PCM 仍写 `sound0`，与片内 DAC 同时输出。
仅接受 44.1/48 kHz、16 位、双声道；PE 组 DO=PE1、BCLK=PE2、LRCLK=PE3、
MCLK=PE4，PA 组对应 PA1..4。外部 codec 需自行配置为兼容的从机。
FM 的 38 kHz 路径不输出到 I2S，停止 FM 后可恢复 PCM/I2S 播放。
新恢复代码、来源、差分记录及限制见 [I2S 分析](../docs/lib-analysis/I2S_RECOVERY.md)。

硬件 I2C 仍缺少可确认的控制器定义或实现对象；新找到的 AB53XX SDK 也只提供
GPIO 软件 I2C，因此没有将其当成硬件驱动接入。原生 USB CDC 软件适配已完成、实机验证待进行；NimBLE Host 和控制器
恢复源码均已接入本综合项目。
控制器恢复期间，原 `.a` 只作为对照和明确标注的阶段性依赖；直接链接原库不作为集成完成标准。

### NimBLE 与 GATT 实机验证（2026-11-02）

默认综合配置已接入 `rtt/nimble` 子模块中的 Apache NimBLE **1.10.0**，
继续使用已恢复源码的控制器，不链接 `libbtctrl.a`。上电后在 UART0 shell 执行：

```text
ble start
ble status
free
```

广播名 **AB32-NIMBLE**，支持一个 BLE 外设连接，断开后自动重新广播。
自定义 GATT 特征支持读、带响应写入和通知；首选 MTU 64，当前关闭配对、绑定与加密。
`ble stop` 只停止当前广播，`ble adv` 恢复广播。与 `btctl test` 诊断模式切换需复位。
服务 UUID、配置和完整使用方法见 [NIMBLE.md](NIMBLE.md)。

**iPhone 扫描方式：**在 nRF Connect 等 BLE 调试 App 中允许蓝牙权限后扫描并连接。
“设置 → 蓝牙”不会列出所有自定义 GATT 设备，列表中没有该设备不等于广播失败；
可参阅 [Nordic 官方说明](https://devzone.nordicsemi.com/f/nordic-q-a/58893/ios-ble-device-list-issue)。
当前已完成的连接验证使用电脑 BlueZ，尚未完成 iPhone/Android App 的实机验收。

最终固件已烧录并通过读回 CRC 校验；保留 PCM 池和不带 PCM 池两种状态下，
各完成三轮连接、服务发现、1/20/60 字节读写与通知、断开及重连。
MTU 协商为 64，60 字节覆盖控制器 27 字节 ACL 的分片/重组；两种状态测试前后堆不下降，
HCI 传输错误及 Host 重置均为零。八组离线驱动测试与 NimBLE、仅控制器、音频三种构建也已通过。

此处为接入 USB 前的实测：总堆 **29736 字节**；NimBLE 运行且尚未使用 PCM 时可用 **11360 字节**，
保留 PCM 播放池时可用 **2864 字节**。FM 启停、PCM 播放及资源互斥回归通过，
但 FM/蓝牙同时使用射频尚未支持，真实广播接收仍待换耳机验证。
内存预算与测试边界见 [MEMORY.md](MEMORY.md#nimble-接入后)，
实机证据见 [验证报告](../docs/lib-analysis/generated/recovery/nimble-rtt-hardware.json)。

### 蓝牙控制器实机测试（2026-11-02）

默认配置启用 `BSP_USING_BTCTRL`，上电保持蓝牙关闭。通过烧录所用的 UART0 shell 执行：

```text
btctl test
btctl adv
btctl status
btctl advoff
btctl scan
btctl status
btctl scanoff
free
```

`test` 停止 FM、初始化 ROM 堆和控制器，并依次检查 HCI Reset、版本、地址及 BLE 缓冲区。
`adv` 发送名为 `AB32-RTT-TEST` 的不可连接广播，静态随机测试地址 `C2:00:AB:32:00:01`；
电脑 BlueZ 已实际收到。`scan` 自动停止广播，进行被动扫描；`status` 显示收到的广播报告数量。
扫描报告只计数，不输出周围设备的地址。以上是控制器独立诊断模式；
使用可连接广播与 GATT 时，复位后改用 `ble start`，不要先执行 `btctl test`。

ROM 默认经典蓝牙冲突检查把空闲时隙判为忙；RTT 板级适配在两种经典连接计数均为零时
允许 BLE 调度，在初始化及 Reset 时通过 ROM 回调注册接口安装此策略。
这轮只验证 BLE；经典蓝牙与 BLE 共存尚未验证。

固定控制器内存保留 `0x11000..0x137ff`，内核、控制器和中断路径从 `0x13800`
及第二块 SRAM `0x50000` 运行。shell 和 PCM 框架的普通线程代码放在 Flash。
接入 NimBLE 前的内存优化版本总堆为 40872 字节；蓝牙启动且尚未打开 PCM 时可用 26416 字节，
首次播放 PCM、保留完整播放池后可用 17920 字节。广播/扫描重复启停后未下降。
这些历史数据不代表当前默认配置；NimBLE 接入后的实际预算见 [内存记录](MEMORY.md)。
蓝牙测试取得射频使用权后，FM 启动/调谐返回忙；广播或扫描停止后仍需复位才能重新运行 FM。
这是同一综合固件内的临时互斥策略，FM/蓝牙同时工作尚未支持。

主机可使用 `python3 tools/rtt/console.py --port /dev/ttyUSB0 'btctl status'` 读取状态。
构建回归新增 `python3 tools/rtt/check_builds.py bluetooth audio`；原始控制器恢复文件未为调试修改。
`python3 tools/rtt/memory_report.py` 检查统一固件的内存段、固定地址及关键函数位置。
`pcmcheck` 通过 `sound0` 播放约一秒静音，验证首次分配、播放和关闭；FM 正在运行时返回忙。
PCM 缓冲池首次初始化后保留供复用，复位后重新按需分配。

### 原生 USB CDC 软件适配

`BSP_USING_USB_CDC` 默认编入综合工程，上电保持关闭。新控制器适配层使用
已恢复的 AB32 寄存器/DMA 行为，上层采用仓库已有 CherryUSB 1.5.2 核心和 CDC 类，
不链接厂家 USB 对象。通过 `usbcdc start`、`usbcdc status`、`usbcdc stop` 控制，
当前应用为二进制原样回传，FinSH 继续使用 CH340/UART0。

V2.0 的原生 USB 经 P5 接 Host 用 USB-A：1–2 接通 PB4/D−，3–4 接通 PB3/D+；
Type-C 的 D+/D− 经 R13/R11 接 CH340E。P5 不是模式切换器，不能把原生 USB 接到 Type-C。
USB-A 的 VBUS 直接接板上 +5V，不能用普通 USB-A 双公线直接连电脑测试 Device；
需要另行确定 PB3/PB4 的 Device 接口引出、共地与供电方案。
**综合固件已烧录，Device 硬件适配及实机枚举、收发仍待完成。**
软件已通过包含真实协议栈的离线测试和综合构建，接线与后续测试步骤见 [USB.md](USB.md)。
USB 初次接入时原始堆区为 28320 字节；加入 TF/FAT 后为 24352 字节，加入 IR 后为 23168 字节，
按键接入时为 22528 字节，RTC 修复时为 22848 字节，加入 S2 呼吸灯后为 22624 字节；
板上红外长按修复版为 22528 字节，实测可用堆 13064 字节；电源驱动 RTC 唤醒实测版为 23360 字节，实测可用 13896 字节，10 轮前后不变。
USB 启动及各模块组合的运行时余量仍需实测，见 [MEMORY.md](MEMORY.md)。
前面的 NimBLE 硬件内存数据属于接入 USB 前的版本。

### TF/FAT 软件适配（2026-11-02）

当前默认 `BSP_USING_TFCARD` 使用原生 SD0 源码、静态 DMA 缓冲和互斥块 I/O，
注册 `tf0`，并接入 DFS v1/FatFs。上电不自动探测，没有 SD 后台线程。
`tf probe` 成功后 `tf mount` 将已有 FAT12/16/32 卷挂到 `/`；
关闭文件后 `tf umount`、`tf stop`，不自动格式化。

TF/FAT 首次接入时的综合工程已编译并生成 DCF，原始堆为 24352 字节，
SRAM1 余 32 字节。FAT 内存模拟卡读写、原生 SD0 模型和回归测试通过。
目前没有 TF 卡，未烧录此版本，实卡及运行时内存仍待验证；
支持范围、接线和下一步测试见 [TF.md](TF.md)，完整预算见 [MEMORY.md](MEMORY.md)。

### 红外接收软件适配（2026-11-02）

`BSP_USING_IRRX` 默认编入原生 NEC/扩展 NEC 接收器，注册 `ir0`，上电不启动硬件。
V02 板短接 P1 的 1–2，将 IRDA 接收输出接至 PE6（IR_G7）；旧版为 J17。
使用 `irrx start`、`irrx status`、`irrx read`、`irrx watch 10`、`irrx stop`。
8 项静态队列报告 DOWN/UP 和重复完整帧；命令反码校验、丢失计数及启停恢复均已加入。
短 NEC 重复码由硬件维护按住状态，不伪造其重复事件。此接口不实现 IrDA 数据链路。

没有新增线程，中断和临界区路径保留 SRAM，普通设备入口和 shell 代码放 Flash。
该次 IR 接入版原始堆区 23168 字节，SRAM1 余 32 字节；暂无遥控器，该阶段未烧录，
当时物理接收和运行时内存待实测。2026-11-03 已用扫地机器人遥控器验证接收，
并修复慢速完整帧重复导致的长按提前释放：main 每 10 ms 检查硬件释放及 400 ms 帧间宽限。
四向键共 53 帧、4 DOWN/49 REPEAT/4 UP，校验错误及队列丢弃均为零。
接线、键码、API、协议限制与验证记录见 [IR.md](IR.md)。

### 三个板载按键

当前综合项目默认使用 `BSP_USING_KEYS`，注册 keys0，在原主线程扫描 S2/PF1、S3/PF0、S4/PA2。
串口执行 `keys watch 10` 后按键，可观察 DOWN、UP 和一次 LONG；`keys status` 查看三键状态
及溢出计数。S1 保持复位用途。每键 30 ms 消抖，稳定按住 800 ms 产生长按事件，
没有新增线程或绑定 FM/蓝牙操作。该次按键接入版原始堆区为 22528 字节，SRAM1 余 32 字节。
GPIO IRQ 改为可选、按需创建线程，因此早期实测的 gpioirq 线程列表不再代表当前默认配置。
引脚冲突、设备 API、GPIO 修复及验证边界见 [KEYS.md](KEYS.md)。
最新综合固件已经 CH340 下载并通过逐块 CRC 校验，RT-Thread 5.2.2 启动正常；
实按三个键均收到 DOWN/UP，KEY2 在 800 ms 产生 LONG，1590 ms 松开。
启动时总堆 22440 字节、可用 13064 字节；本轮没有验证 USB 枚举、TF 实卡或红外接收。

### 离线验证

在仓库根目录执行：

```sh
python3 tools/rtt/test_drivers.py
python3 tools/rtt/test_usb.py
python3 tools/rtt/test_tf.py
python3 tools/rtt/test_ir.py
python3 tools/rtt/test_keys.py
python3 tools/rtt/test_rtc.py
python3 tools/rtt/check_builds.py default rtc_integrated rtc rtc_external keys_irq ir_integrated ir tf usb nimble bluetooth audio sd
python3 tools/rtt/memory_report.py
# 更多外设配置：default peripherals pwm flash spi_fm spi_fm_i2s i2s_pa
```

`test_drivers.py` 用真实 RT-Thread 头文件与模拟寄存器运行 BSP 回调，覆盖 SPI 超时/tick 回绕/片选、
ADC 错误、TIMER 共享中断和溢出、WDT 秒单位、RTC 时间范围、音频参数/PLL/停止、
Flash 分页/返回值，以及 SD 命令和数据完成事件、超时/CRC/缓存边界。
另覆盖 FM 状态机、音频入口互斥、超时释放/重启、I2S 输出启停与格式限制，
以及 NimBLE NPL 队列/定时器和 HCI 传输缓冲的边界与失败路径。
它不验证硬件时序、DMA 电气行为或真实卡片协议。

`test_usb.py` 以模拟寄存器/DMA 驱动真实 CherryUSB 核心、CDC 和 AB32 适配代码，
覆盖控制传输、地址/配置、回传/背压、STALL、越界和复位，并启用地址/未定义行为检查。

`test_tf.py` 使用真实 TF 适配层、DFS disk glue 和 FatFs，在内存模拟卡上验证 FAT12/16/32
读写、同步、重挂、日期及错误传播，并重跑 SD0 的 34 个场景、1217 个断言；不访问真实存储卡。

`test_ir.py` 以模拟 MMIO/W1C/映射寄存器驱动真实红外接收代码，验证事件、校验、队列溢出、
重复启停、引脚/时钟恢复、时限观察及冲突配置拒绝；没有遥控器或光学链路参与。

`test_keys.py` 覆盖 GPIO/HAL 及三键真实代码，检查共享中断、正确恢复中断状态、回调期间的新中断、
解除/重新挂接、W1C、消抖、长按、同时按键、队列溢出和 tick 回绕；使用模拟电平，无物理按键参与。

`test_rtc.py` 使用真实 RTC 命令编码和模拟传输桥，验证时钟选择、首次校时、计数保留、跨秒读取、
逐字节故障注入与中断/互斥恢复；`test_drivers.py` 也会调用该测试。没有实际振荡器或备电保持参与。

`check_builds.py` 在 `/tmp/ab32-rtt-build-*` 的独立源码副本中执行指定配置的 GCC 完整构建，
保留各配置 ELF、map 和日志，并检查 FM 未链接回原库 FM 对象，不修改工作目录的 `rtconfig.h`，也不烧写设备。
`peripherals` 包含 SPI1、SARADC、RTC、WDT 和 TIMER1..5；其他冲突外设分别构建。

## 维护人信息

- [greedyhao](https://github.com/greedyhao)
