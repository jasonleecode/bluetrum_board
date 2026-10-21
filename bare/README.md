# AB32VG1 裸机工程

`bare` 目录是不带操作系统的 AB32VG1 示例工程，不依赖 RT-Thread，也没有线程、调度器、设备框架等运行时。程序从 `startup.S` 进入 C 入口后直接执行 `app` 下的业务代码，适合验证芯片外设、闭源库接口和小型单任务程序。

由于 AB32VG1 官方提供的驱动库是闭源的，本工程基于对官方 `libhal.a` 的分析，整理出部分驱动接口，并在 `drivers` 目录下做了简单封装。

## 目录说明

- `app/`: 裸机应用示例。每个 `*.c` 文件会被 `Makefile` 编译成一个独立目标。
- `drivers/`: 对 UART、ADC、FM、USB、SPI、I2C、Timer 等外设接口的轻封装。
- `lib/`: 厂家闭源静态库，例如 `libhal.a` 和 `libbtctrl.a`。
- `startup.S`: 裸机启动代码。
- `link.ld`: 裸机链接脚本。
- `Makefile`: 使用 RISC-V GCC 工具链构建 `app` 下的示例程序。

## 编译

默认工具链前缀是 `riscv64-unknown-elf-`，如需使用其他工具链，可以通过 `CROSS_COMPILE` 覆盖：

```sh
make
make CROSS_COMPILE=riscv64-unknown-elf-
```

编译后会在 `build/` 下生成对应的 `.elf` 和 `.bin` 文件。例如 `app/main.c` 会生成：

```text
build/main.elf
build/main.bin
```

清理构建产物：

```sh
make clean
```

## FM 驱动恢复

FM 默认使用厂家库，也可选择已恢复的 10 个底层函数（在 `bare/` 下执行）：

```sh
make FM_BACKEND=recovered
# 同时使用恢复的硬件 SPI1
make SPI_BACKEND=recovered FM_BACKEND=recovered
```

对应输出目录分别是 `build/recovered-fm/` 和 `build/recovered-spi/recovered-fm/`。恢复代码仍依赖平台延时，完整工程仍使用其他 SDK 接口。现有验证覆盖原库差分和独立链接，尚未证明完整固件能在板上启动、收音。

封装中的频率单位仍为 kHz，内部已修正为两参数 PLL 调用及 10 kHz 单位：

```c
#include "drv_fm.h"

/* 平台时钟及 DAC/audio buffer 必须另行正确初始化。 */
drv_fm_cfg_t cfg = {.frequency_khz = 101100, .rf_cap = 0x10};
if (drv_fm_init(&cfg)) {
    drv_fm_start();
    drv_fm_route_to_audio_buffer(true);
    /* 可通过 drv_fm_tune(87500) 手动切换到 87.5 MHz。 */
}
```

支持的参数范围是 76–108 MHz，步进 10 kHz；`rf_cap` 仅允许 0–31，其最佳值仍需板级验证。原库的 PLL 等待无超时，时钟或射频未就绪时初始化、调谐可能不返回。

`app/main.c` 和 `app/usb_fm.c` 仍是调用流程骨架，DAC/缓冲区初始化及实际 FM→USB 音频链路尚未验证。DMA 接口只启动传输，计数单位、样本格式和完成条件仍待确认。收音需插入 CTIA 标准 3.5 mm 耳机线作为天线；搜台/RSSI 尚未恢复。证据、测试命令和限制见 [FM 恢复记录](../docs/lib-analysis/FM_RECOVERY.md)。

## 蓝牙控制器恢复

`drv_bluetooth` 提供 HCI 控制器接口，尚未提供可直接使用的 GAP/GATT 主机功能。两个 HCI 消息对象和部分中断适配已恢复为 C，可在 `bare/` 下选择：

```sh
make BT_SUPPORT_BACKEND=recovered
```

输出在 `build/recovered-bt-support/`，也可与 SPI/FM 恢复选项组合。仍依赖原库的控制器、射频和芯片 ROM；这是部分支持层恢复。

当前裸机骨架没有满足蓝牙固定 RAM、ROM 堆初始化及真实 IRQ 分发要求，因此 `drv_bt_init()` **默认返回 false**。板级实现需要覆盖 `drv_bt_platform_init()`，完成这些前提后才能返回 true。不能仅修改返回值跳过检查。平台依赖及已有 SDK 参考路径见 [蓝牙恢复记录](../docs/lib-analysis/BLUETOOTH_RECOVERY.md)。

初始化成功后须在主循环持续调用 `drv_bt_run_loop()`。`drv_bt_isr()` 只标记待处理工作，主循环再处理并重新使能控制器中断。接收回调中的数据包含 H4 类型字节，缓冲区仅在回调期间借用；需保留时先复制，不能在回调里重入控制器。

发送命令仍需主机管理信用与完成事件；ACL 只接受一个容量以内的分片。底层返回 true 不可靠地代表排队成功，封装也不代表已建立无线连接。本轮验证为离线机器码、封装与独立链接检查，完整固件启动、广播、扫描、连接尚未实机验证。

## 生成下载文件

裸机程序编译完成后，`build/*.bin` 不能直接交给串口烧录工具烧写。它只是用户程序输入文件，需要先将用户程序 `bin` 和固定的 `header.bin` 合成为最终的 `.dcf` 下载文件。Linux 可使用本仓库恢复的打包工具（从仓库根目录执行）：

```sh
tools/linux/convert tools/header.bin bare/build/main.bin /tmp/main.dcf
python3 tools/linux/bluetrum_cli.py inspect /tmp/main.dcf
```

默认打包路径已通过原版机器码输出对照。Linux 串口下载命令也已实现，但尚未上板验证；使用范围和接线适配选项见 [Linux 工具说明](../tools/linux/README.md)：

```sh
tools/linux/downloader flash /tmp/main.dcf --dry-run
tools/linux/downloader flash /tmp/main.dcf --port /dev/ttyUSB0
```

仓库顶层 `tools/final_bin.xm` 的逻辑是将 `header.bin` 和 `user.bin` 合成为 `final_bin.dcf`。使用时可以先把裸机目标 `.bin` 准备成 `user.bin`，再执行：

```sh
riscv32-elf-xmaker.exe -b final_bin.xm
```

生成的 `final_bin.dcf` 再交给厂家 Windows 版 `Downloader.exe` 通过串口烧录。`tools/XMAKER.md` 记录了目前对 `.xm` 脚本和 `.dcf` 合成格式的分析。

`rtthread.xm` 属于 RT-Thread 工程流程，裸机工程不需要依赖 RT-Thread 的构建产物。
