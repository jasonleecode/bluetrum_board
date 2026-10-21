# SD 卡原生硬件读写

本轮把裸机的 SPI 卡协议封装替换为 **SD0 控制器的 1-bit 原生 SD 总线驱动**，实现初始化、容量查询、512 字节单块读写。命令、CRC 和 DMA 由硬件完成，不使用软件 SPI，也不调用 `drv_spi`。

目前通过 34 个主机模型场景、1217 个断言和 RV32 编译/独立链接；没有接 SD 卡实测。实际卡识别、波形、DMA 地址映射和掉电后的数据持久性仍待验证。

## 证据与重建边界

这部分并不是从 `.a` 中逐函数还原的：检查 `bare/lib/libhal.a` 成员没有找到 SD/MMC 卡驱动对象，`sdadc.o` 是音频 Sigma-Delta ADC，和 SD 卡无关。仓库已经包含足够的硬件原始资料，直接以这些资料重建更合适。

| 本地证据 | 能确认的内容 |
| --- | --- |
| [寄存器手册第 9 章](../ab32vg1_doc/AB32VG1_Register.pdf) | SD0 控制器、命令/数据完成标志、CRC 状态、DAT0 忙检测、DMA 流程、分频、21-bit DMA 地址；硬件支持 1/4-bit SD 和 MMC |
| [RT-Thread SDIO 驱动](../../rtt/bsp/libraries/hal_drivers/drv_sdio.c)及[状态定义](../../rtt/bsp/libraries/hal_drivers/drv_sdio.h) | 接收 DMA 必须先于读命令准备，发送 DMA 在写命令响应后启动；每次 512 字节；写完成检查 CRCS=2 |
| [RT-Thread SD 卡协议实现](../../rtt/components/drivers/sdio/dev_sd.c) | ARG3..ARG0 到 CSD 的位序、CSD v1/v2 容量解析及最大数据速率 |
| [板级引脚配置](../../rtt/bsp/board/ab32vg1_hal_msp.c)及[V02 原理图 P2/TF3](../ab32vg1_doc/AB32VG1_Prougen_Schematic_V02.pdf) | G2 映射，PB0=CMD、PB1=CLK、PB2=DAT0；原理图通过 P2 跳线连接 TF 卡槽 |
| [HAL SD 源码](../../rtt/bsp/libraries/hal_libraries/ab32vg1_hal/source/ab32vg1_hal_sd.c) | 可用于交叉检查寄存器操作，但 `hal_sd_init` 固定失败、读操作未实现，且有命令参数/容量计算问题，不能直接当作完整驱动使用 |

实现位于 [drv_sdcard.c](../../bare/drivers/src/drv_sdcard.c)，公共接口见 [drv_sdcard.h](../../bare/drivers/inc/drv_sdcard.h)。这是新写的控制器/卡协议驱动，测试结果不能称为与某个原库对象逐指令等价。

## 已实现的功能

- 原生 SD0、G2 引脚、1-bit 总线、轮询完成、DMA 数据收发。保留连续 SD 时钟以便卡结束 DAT0 忙状态，不注册中断。
- 初始化：CMD0 → CMD8 → CMD55/ACMD41 → CMD2 → CMD3 → CMD9 → CMD7 → CMD13。SDSC 额外发送 CMD16，固定块长 512 字节。
- SD v1 SDSC、SD v2 SDSC、使用 CSD v2 的 SDHC/SDXC。CMD8 必须通过回显/CRC 检查；只有硬件完成并报告无响应时才尝试旧卡路径，控制器超时不伪装成旧卡。
- SDSC 使用字节地址，SDHC/SDXC 使用块地址；检查 CSD/OCR 一致性、容量边界、RCA、读写命令类和卡状态。
- 容量使用 64-bit 块数，能表示 CSD v2 上限的 `2^32` 个 512 字节块；LBA 参数为 32-bit，因此可访问最后一块 `0xffffffff`。
- CMD17 单块读取：先启动接收 DMA，命令/数据 CRC 均成功后才复制到调用者缓冲区；失败不复制脏数据。
- CMD24 单块写入：发送成功后启动 DMA，检查卡返回 CRC 状态、等待 DAT0 解除忙状态，再通过 CMD13 确认 READY_FOR_DATA/TRAN。失败的写操作仍可能已经改变卡内容，不自动重试。
- 真正的微秒时间回调控制命令、数据及初始化超时，支持 32-bit 时间回绕；可选卡检测和写保护回调，以及 CSD 写保护位检查。
- 调用者数据缓冲区可不对齐；驱动使用专属 DMA staging 缓冲区。参数错误、越界和写保护错误保留就绪状态；传输错误/移除卡会关闭 SD0，要求重新初始化。

尚未实现：4-bit、MMC/eMMC、SDIO 功能卡、UHS/SDUC、多块 CMD18/CMD25、擦除、分区或文件系统、热插拔自动挂载。SDHC/SDXC 的协议解析支持不等于已经完成不同实卡兼容性验证。

## 板级接入要求

`drv_sdcard_cfg_t` 现在是原生 SD 配置，旧的 `set_cs`、`spi_baudrate` 字段已删除。仓库应用没有调用这两个旧字段；外部调用者需要迁移。

1. **固定源时钟。** `source_clock_hz` 必须是真实系统/SD 源时钟，在驱动使用期间保持不变。SD0BAUD 只有 8 位且不能为 0，实际频率是 `source/(BAUD+1)`。本实现接受 200 kHz～102.4 MHz 源时钟，初始化不超过 400 kHz；120 MHz 不能仅靠该分频器降到 400 kHz，因此明确拒绝。传输默认 12 MHz，同时限制在 CSD 最大速率和 25 MHz 内，不做高速模式切换。
2. **真实计时和延时。** 必须提供 `time_us(context)` 和 `delay_us(context, us)`。`time_us` 必须持续前进；现有 `vendor_shim.c` 的 `hal_get_ticks()` 是调用计数，不能直接使用。命令超时默认 100 ms，数据/编程超时默认 1 s，自定义上限 5 s；ACMD41 等待就绪约 1 s。
3. **真实 DMA RAM。** 提供 32 字节对齐、至少 512 字节、连续、CPU/DMA 一致或非缓存的专属 RAM，以及经过验证的总线地址 `dma_address`。驱动只检查对齐和 21-bit 地址窗口，不能判断该地址是否真的指向 RAM。缓冲区须一直有效到 `deinit`，不能与读写 API 的调用者缓冲区重叠。此驱动没有缓存维护操作。
4. **引脚、卡供电与独占。** 驱动启用 SD0/GPIO 时钟门，配置 PB0/PB1/PB2 的 G2 复用和上拉。板级先建立系统时钟、电源及正确卡槽接线；P2 跳线要与原理图一致。SD0 和这三个引脚不能同时由其他驱动使用。`deinit` 停止 SD0，但不恢复此前的 GPIO/时钟门配置。
5. **串行调用。** 驱动全局单实例、同步阻塞，不可重入；不能在 ISR 或自己的回调中调用，也不能与 RT-Thread SDIO 驱动同时操作控制器。

特别注意：当前裸机 [link.ld](../../bare/link.ld) 把普通 RAM 放在 `0x20000000`，超出 SD0 的 21-bit DMA 地址范围。**不能把普通静态数组地址直接强转、截低 21 位后交给 DMA。** 必须先为真实板子确认内存布局，放置可被 SD0 访问的缓冲区，或验证实际 CPU/总线地址映射。下面例子有意通过板级函数取得缓冲区，不声称当前通用链接脚本已具备可运行的内存布局。

## 只读接入示例

以下 `board_*` 都需要板级实现；未提供满足契约的计时、RAM 和供电之前，不应把它们写成总是成功的空桩。第一步只读识别和读取，不写 sector 0。

```c
#include "drv_sdcard.h"

/* Board implementation must supply these from real clocks and mapped RAM. */
extern uint32_t board_sd_source_hz(void);
extern uint32_t board_time_us(void *context);
extern void board_delay_us(void *context, uint32_t us);
extern uint8_t *board_sd_dma_buffer(void);       /* aligned, dedicated 512 bytes */
extern uint32_t board_sd_dma_bus_address(void);  /* verified, not pointer truncation */

bool inspect_card(drv_sdcard_info_t *info, uint8_t sector[512])
{
    drv_sdcard_cfg_t cfg = {
        .source_clock_hz = board_sd_source_hz(),
        .time_us = board_time_us,
        .delay_us = board_delay_us,
        .dma_buffer = board_sd_dma_buffer(),
        .dma_address = board_sd_dma_bus_address(),
    };
    if (!drv_sdcard_init(&cfg)) return false;
    bool ok = drv_sdcard_get_info(info) && drv_sdcard_read_block(0, sector);
    /* Query drv_sdcard_last_error() here if !ok; deinit clears the error. */
    drv_sdcard_deinit();
    return ok;
}
```

容量字节数为 `info.block_count * 512`，计算保持 64-bit。块层不解释 MBR/GPT/FAT/exFAT，也不保留文件系统元数据；写入测试应由调用者明确选择备份过的测试介质/区域。

驱动由 `bare/Makefile` 现有源文件收集规则自动编译，无需新增 backend 开关；选择 vendor/recovered SPI 都不会改变 SD0 路径。

## 离线验证及局限

运行：

```sh
python3 docs/lib-analysis/test_sdcard.py --linker /path/to/ld.lld
```

- [主机模型用例](tests/sdcard_test.c)：34 个场景、1217 个断言。包括 SDSC v1/v2、SDHC、CSD v2 最大容量、LBA 寻址、非对齐调用缓冲区、逐字节数据检查、命令/数据 CRC 错误、卡移除、写保护、卡忙/编程超时、CMD8 错误、清完成标志同时丢失 NRPS 的旧卡路径、不支持的 CSD/时钟/DMA 地址、时钟分频和计时回绕。
- RV32IMAC/ILP32：`clang -Os -ffreestanding -Wall -Wextra -Werror` 编译；独立链接保留全部 6 个公共 API，只需要 `vendor_shim` 的内存复制/清零函数，不链接 `libhal.a`、`libbtctrl.a` 或 SPI 驱动。
- [验证记录](generated/recovery/sdcard-validation.json)保存实现、测试及本地硬件证据的 SHA-256。

主机模型的卡响应、DMA 完成、CRC 和忙信号都是脚本提供的，只验证驱动状态机和失败处理，无法证明真实控制器时序或物理存储成功。独立链接使用测试布局，不能据此认为当前裸机启动/链接已可上板。

下一步应先确认真实 DMA RAM、系统时钟及 P2 卡槽接线，分别对一张 SDSC 和一张 SDHC 卡执行只读 CID/CSD/容量与扇区比较，再对专用测试介质执行写入、重新读回及重新上电核验；文件系统接入放在块层实测之后。
