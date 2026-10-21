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

优先使用仓库 `.toolchains/riscv64-unknown-elf-gcc-10.1.0-2020.08.2-x86_64-linux-ubuntu14/` 下的 SiFive GCC 10.1.0；没有该目录时使用 PATH 中的 `riscv64-unknown-elf-`。如需使用其他工具链，可以通过 `CROSS_COMPILE` 覆盖：

```sh
make
make CROSS_COMPILE=riscv64-unknown-elf-
```

Linux x86_64 配套包来自 [SiFive 官方发布](https://github.com/sifive/freedom-tools/releases/tag/v2020.08.0)，与 [Bluetrum SDK 文档](https://bluetrum-sdk.readthedocs.io/zh/latest/getting-start/beforehand.html) 列出的 GCC 10.1.0 版本一致。安装到仓库内不需要修改系统 PATH（从仓库根目录执行）：

```sh
mkdir -p .toolchains
tar -xzf "$HOME/Downloads/riscv64-unknown-elf-gcc-10.1.0-2020.08.2-x86_64-linux-ubuntu14.tar.gz" -C .toolchains
make -C bare
```

工具链目录已被 Git 忽略。本次安装包 SHA-256 为 `8d2a6d2c123b9c7fe30d446bace4fa3d74576715e484ddfafbb491cb6082ebc5`（本地计算，用于复现，不是额外的官方签名验证）。默认后端及五个恢复后端同时开启时，四个应用的 GCC 编译、链接和 GNU objcopy 生成均已通过。链接脚本已将 `.sdata/.sbss` 纳入启动复制/清零区间；真实芯片内存布局、启动与 IRQ 接入仍待板级移植，构建成功不代表能够直接烧录运行。

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

## USB 驱动恢复

控制器/连接检测、端点、EP0 状态机和 HID 共 33 个函数已恢复，可在 `bare/` 下选择：

```sh
make USB_BACKEND=recovered
```

输出在 `build/recovered-usb/`，也可与 SPI/FM/蓝牙选项组合。默认仍使用厂家库；恢复选项替换五个库对象，仍保留原库的设备生命周期代码。

`drv_usb_init(DRV_USB_DEVICE)` **默认返回 false**：当前裸机骨架缺少完整板级接入与标准请求派发。需实现 `drv_usb_platform_init()`、`drv_usb_platform_poll()` 及 `os_mq_ude_*_post` 钩子，落实时钟/引脚、DMA 内存和代码段、定时器、串行化的中断处理，以及描述符和 SETUP 请求处理。仅将初始化钩子改为返回 true 不会实现枚举。

现有 HID 描述符只有单字节多媒体按键输入报告，不能直接作为 CDC 串口或任意双向数据接口。平台完成枚举后，`drv_usb_hid_try_send()` 复制一个字节并排队，通过持续调用 `drv_usb_run_loop()` 推进；按键释放需另发零报告。`drv_usb_hid_busy()` 变为 false 也可能源于复位/中止，不是主机接收确认。

Host 模式目前返回 false；USB 音频初始化和发送也返回 false。旧封装误把端点编号当作状态指针，且遗漏了初始化上下文参数，现已移除这些无效调用。`app/usb_fm.c` 会在 USB 音频未实现时返回失败，不再启动 FM 后假报音频发送成功；FM 到内部 audio buffer 的路由不等于 USB 音频上传。

1066 项原库差分测试、封装测试和独立链接检查已通过，尚未验证实机 DMA、枚举或 Linux 通信。接口布局、描述符限制与后续接入步骤见 [USB 恢复记录](../docs/lib-analysis/USB_RECOVERY.md)。

## SD 卡块读写

`drv_sdcard` 已改为片上 **SD0 原生一位总线**，使用板载卡槽 G2：PB0=CMD、PB1=CLK、PB2=DAT0，不再走原来的 SPI 卡协议。支持 SDSC、SDHC/SDXC 的初始化、容量查询及 512 字节单块读写，当前不含文件系统。

配置接口已改变：原 `spi_baudrate/set_cs` 替换为真实 SD 源时钟、微秒计时/延时回调及专用 512 字节 DMA 缓冲和总线地址。DMA 缓冲需 32 字节对齐、缓存一致，地址满足控制器的 21 位窗口；不能直接截断 `bare/link.ld` 的 `0x20000000` 指针。原配置调用者必须迁移。

`drv_sdcard_get_info()` 返回块数、寻址方式、实际时钟和写保护状态；`drv_sdcard_last_error()` 区分参数、CRC、超时、卡状态等错误。读数据只在校验成功后复制给调用者；写操作等待卡退出忙态并确认 ready/TRAN。传输失败后需重新初始化，失败写入也可能已经改变卡内容。

控制器模型与 RV32 编译检查已通过，真实卡尚未验证。接线、只读调用示例、错误处理及限制见 [SD 恢复记录](../docs/lib-analysis/SD_RECOVERY.md)。

## 本地音频输入与输出

模拟输入和 SDADC 的 18 个函数可选择恢复后端：

```sh
make AUDIO_BACKEND=recovered
```

输出在 `build/recovered-audio/`，可与其他后端组合。该选项替换 `audio.o` 和 `sdadc.o`；本地 DAC 输出代码来自公开 BSP，两个后端均可使用。

`drv_audio_init()` 现在初始化本地 **48 kHz、双声道、16 bit DAC**，不再尝试初始化 USB 音频。需实现 `dac_output_platform_prepare()`，提供真实 26 MHz 参考时钟前提、延时、已知 RAM 内的 DMA 映射和资源独占；默认返回失败。初始化成功保持静音，显式设置音量并解除静音后播放。

使用 `drv_audio_write_frames(samples, frames)` 提交 PCM，每帧两个 `int16_t`。返回实际入队帧数，FIFO 满时可返回零，调用者只重试未接受的尾部；入队不等于已播放。`drv_audio_stop()` 中止队列。旧 bool 型 `drv_audio_write()` 保留为 USB-only 接口，当前返回失败，避免丢失部分写入的长度信息。示例见 `app/audio_play.c`。

采集侧 `drv_adc_init()` 将 MIC 映射到原库单 bank 编码，gain 改为 16 位。通过 `drv_adc_set_dma_samples()` 配置非零原始计数，再使用 `drv_adc_start_dma(buffer, capacity_bytes)` 明确传入缓冲。板级必须实现 ADC 平台钩子、验证 DMA 计数单位/容量/地址，并接入 `drv_adc_isr()`；默认不启动。旧无缓冲 `drv_adc_start()` 返回失败，`app/mic_read.c` 已迁移为显式缓冲的事件探测示例。

目前未确认采集 PCM 布局或半/满缓冲事件，AUX 胶水、I2S 控制器和 USB 音频也尚未实现。本地 DAC、FM、SDADC 共享音频时钟/资源，当前不要并发启用。差分、平台依据和集成方法见 [音频恢复记录](../docs/lib-analysis/AUDIO_RECOVERY.md)。

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

## I2S 恢复代码补充

`drivers/src/i2s_src_recovered.c` 新增同系列 AB53XX SDK 的硬件 SRC 输出子集，
供 RT-Thread 音频接入复用。当前没有裸机 I2S 应用；调用者仍需完成真实音频时钟、
时钟门控和 PCM 音源初始化。范围、差分测试和芯片兼容性限制见
[I2S 恢复记录](../docs/lib-analysis/I2S_RECOVERY.md)。
