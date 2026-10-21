# AB32VG1 FM 驱动恢复

本次恢复了 `bare/lib/libhal.a` 中 `fmrx_ang.o`、`fmrx.o` 的全部 10 个 FM 导出函数，并修正裸机封装的调谐 ABI。恢复的是片上硬件 FM 控制代码，不是软件解调器。

当前已完成原库机器码差分、封装回归和独立 RV32 链接检查；尚未验证实际接收、音频输出或自动搜台。这里的“恢复”表示在所测输入和模拟寄存器条件下保持原库行为，不表示已完成整机收音。

## 证据与实现

| 原库成员 | SHA-256 | 恢复源码 |
| --- | --- | --- |
| `fmrx_ang.o` | `add90fc5972abf4ac16c38fdef7e1980e890b5d9c775a66f11d8c1dc1fc73709` | [fmrx_analog_recovered.c](../../bare/drivers/src/fmrx_analog_recovered.c) |
| `fmrx.o` | `f4ecad3933db2792ad6327f45d446d47d4e0aadb2a3d66f82423fcdf4a55430c` | [fmrx_digital_recovered.c](../../bare/drivers/src/fmrx_digital_recovered.c) |

统一的底层声明在 [fmrx_hal.h](../../bare/drivers/inc/fmrx_hal.h)，面向应用的封装在 [drv_fm.h](../../bare/drivers/inc/drv_fm.h)。

| 接口 | 原库中可确认的行为 |
| --- | --- |
| `fmrx_analog_init()` | 模拟寄存器初始化，调用 `hal_mdelay`，内部以 `(1, 8750)` 调谐 |
| `fmrx_set_pll(mode, frequency_10khz)` | 配置频率相关寄存器，并等待 `0x42c` bit 7 |
| `fmrx_set_rf_cap(value)` | 清除 `0x414` 低 5 位，再将参数原样 OR 入寄存器 |
| `fmrx_power_on(val)` | 调用模拟初始化，然后设置数字通路及 DAC 相关控制；此版本忽略 `val`，SDK 传 0 |
| `fmrx_power_off()` | 依次关闭 DMA、数字通路及模拟控制 |
| `fmrx_digital_start()` | 设置 `0x440`、`0x140` 中的使能位 |
| `fmrx_digital_stop()` | 清除相应数字输出位 |
| `fmrx_get_audio_data(buffer, count)` | 设置 DMA 地址、`count - 1` 和启动位，立即返回 |
| `fmrx_dma_to_aubuf(enable)` | 非零时设置 `0x440` bit 2；零时写 `0x444 = 3` |
| `fmrx_dac_sync(buffer_count)` | 比较音频 FIFO 水位，根据三分之一、三分之二阈值写入调整值 |

接口声明与调用方式也参考了 SDK [api_fmrx.h](../../rtt/bsp/libraries/hal_libraries/ab32vg1_hal/include/api_fmrx.h) 和 [fmrx_test1.c](../ab32vg1_doc/test_program/fmrx_test1.c)。ABI 以当前归档中的实际机器码为准。

## 调谐 ABI 修正

原 `drv_fm.c` 将函数声明为 `fmrx_set_pll(uint32_t freq_khz)`，只传入一个 kHz 参数。实际函数读取 `a0`、`a1` 两个参数：第一个参与频率偏移计算，第二个以 **10 kHz** 为单位。模拟初始化在对象偏移 `0x296` 调用它时传入 `(1, 8750)`，即默认 87.5 MHz；频段分支阈值为 `9350`，即 93.5 MHz。

因此，应用配置 `101100 kHz` 时应调用：

```c
fmrx_set_pll(1, 10110); /* 101.1 MHz */
```

第一个参数的完整射频语义尚未建立，封装沿用原库初始化使用的 `1`，不把它命名为未经证实的注入模式。

频率控制字的精确 32 位算法是：

```c
uint32_t x = frequency_10khz * 10u + mode * 228u;
uint32_t word = x * 2581u + (x * 2864u) / 26000u;
```

对于正常频率及模式参数，等价于 `floor((frequency_10khz * 10 + mode * 228) * 2^26 / 26000)`。恢复源码保留原库的 32 位中间乘法溢出行为；不能对任意输入直接替换成 64 位公式。

`drv_fm_init()` 和新增的 `drv_fm_tune()` 接受 `76000..108000 kHz`、步进 10 kHz 的输入，使用范围来自 [DataSheet 的 FM Tuner 项](../ab32vg1_doc/AB32VG1_DataSheet.pdf)。这只是参数范围检查，不代表检测到了电台。初始化还拒绝大于 31 的 `rf_cap`，避免原库未屏蔽的 OR 操作修改相邻字段。

原库对 `0x42c` bit 7 的等待没有超时，恢复代码保留此行为。该位根据使用方式推断为 PLL 就绪/锁定状态，公开资料未给出其正式定义。平台时钟或射频未就绪时，初始化和调谐可能一直不返回。

## 寄存器访问和音频限制

下表描述机器码的访问用途；未公开的寄存器继续使用地址，不杜撰寄存器名称。

| 地址 | 观察到的用途 |
| --- | --- |
| `0x114` | SDK 定义的 `AUBUFFIFOCNT`，供同步函数读取水位 |
| `0x140` | SDK 定义的 `DACDIGCON0`，FM 代码操作 bit 6 |
| `0x160` | 同步函数写 `0xfffe`、`0` 或 `1`，具体调节单位未确认 |
| `0x400 / 0x404 / 0x408` | DMA 缓冲地址、计数减一、启动/停止 |
| `0x410..0x42c` | 模拟初始化及 PLL 配置、等待状态 |
| `0x440 / 0x444` | 数字通路与音频缓冲路由相关控制 |

`fmrx_get_audio_data()` 没有等待完成，也没有返回实际长度。DMA 计数单位、样本格式、声道排列、完成标志和中断尚未确认。新增 `drv_fm_capture_raw()` 原样传递硬件计数，拒绝空指针及零计数；缓冲区尺寸、对齐和生命周期仍需根据后续硬件证据确定。旧的 `drv_fm_capture_to_buffer()` 保留兼容，但参数名 `samples` 不足以证明它表示 16 位 PCM 样本；`drv_fm_get_audio()` 仍只启动传输并返回 0。

`fmrx_dma_to_aubuf(0)` 并不直接清除 `0x440` 的路由位，而是写 `0x444 = 3`。在该寄存器语义确定之前，不能声称这会关闭路由；停止数字输出应使用 `drv_fm_stop()`。

`fmrx_dac_sync()` 令 `third = buffer_count / 3`：第一次 FIFO 读取小于等于 `third` 时写 `0xfffe`；否则再次读取 FIFO，大于等于 `2 * third` 时写 `1`，其余情况写 `0`。恢复实现保留两次独立的 volatile 读取，以允许水位在两次读取之间变化。

SDK 的收音示例先打开 `sound0`、配置 38000 Hz、启用扩展混音并启动播放，之后才开启 FM 和缓冲路由，每 100 ms 调用 `fmrx_dac_sync(total_size_bytes / 4)`。可参阅已有的 [RT-Thread 音频驱动](../../rtt/bsp/board/ports/audio/drv_sound.c)。裸机 `main.c`、`usb_fm.c` 目前仅是调用流程骨架，缺少经过验证的 DAC/音频缓冲配置；常量 `512` 不能替代实际缓冲区配置，也尚未证明 FM 到 USB 的音频链路。

开发板 [User Manual §2.5](../ab32vg1_doc/AB32VG1_User_Manual.pdf) 明确要求插入 **CTIA 标准 3.5 mm 耳机线**，由耳机线充当 FM 天线。即使驱动控制流程正确，后续收音测试仍需这项硬件条件。

## 构建选择

默认继续使用厂家库中的 FM 实现。要用恢复源码替代两个 FM 对象，在仓库根目录执行：

```sh
make -C bare FM_BACKEND=recovered
```

输出目录为 `bare/build/recovered-fm/`。同时恢复 SPI 时：

```sh
make -C bare SPI_BACKEND=recovered FM_BACKEND=recovered
```

输出目录为 `bare/build/recovered-spi/recovered-fm/`。分开目录避免切换后端时复用旧对象。

FM 恢复源码仍依赖平台的 `hal_mdelay()`，整个工程仍会使用其他厂家库接口。此次独立链接检查不等于完整固件可启动；裸机启动、链接地址、时钟、DAC 和板级行为仍需验证。

## 离线验证

验证环境使用 Clang 的 RV32IMAC 目标、支持 RISC-V 的 LLD、Unicorn 2.1.4 和 pyelftools。安装好依赖后，从仓库根目录运行；若 `ld.lld` 不在 PATH，追加 `--linker /path/to/ld.lld`：

```sh
python3 docs/lib-analysis/test_fm_analog_recovery.py
python3 docs/lib-analysis/test_fm_digital_recovery.py
python3 docs/lib-analysis/test_fm_integration.py
```

| 检查 | 结果与范围 |
| --- | --- |
| 模拟部分差分 | **222 项通过**：初始化、93.5 MHz 分界、参数边界、32 位溢出、RF 电容、连续调谐和状态位始终不就绪 |
| 数字部分差分 | **110 项通过**：开关机、启停、路由、DMA 参数、随机初始寄存器、水位边界及两次读取间的变化 |
| 封装回归 | 101.1 MHz 必须传 `(1, 10110)`，上下限、非法参数无硬件调用、DMA 守卫和生命周期通过 |
| 构建选择与链接 | 4 种 SPI/FM 后端组合及非法选项检查通过；封装与 10 个恢复接口链接通过，未从归档抽取 `fmrx.o` 或 `fmrx_ang.o` |

差分测试让原始和恢复后的 RV32 机器码运行在相同模拟寄存器模型上，比较有序读写、延时调用与最终状态。原库的自定义恢复指令使用 ABI shim，`hal_mdelay` 只记录参数；数字部分测试将模拟初始化替换为桩，模拟部分另行独立验证。它们不验证自定义指令本身、真实 PLL、模拟电路、DMA 完成、样本格式或时序。

保留的结果含原对象与恢复源码哈希，见 [模拟差分结果](generated/recovery/fm-analog-differential.json)、[数字差分结果](generated/recovery/fm-digital-differential.json) 及 [模拟部分反汇编](generated/recovery/fm-analog.llvm.txt)。独立链接使用延时符号桩，不是完整固件构建。

## 后续工作

- [ ] 补齐裸机平台时钟、DAC 与音频缓冲配置，使用耳机天线验证手动调频和收音。
- [ ] 确认 DMA 计数单位、格式、完成条件与缓冲约束。
- [ ] 恢复 RSSI、有效台判定、自动搜台以及去加重配置。

DataSheet 宣称支持自动搜台、RSSI 与 50/75 µs 去加重。当前 `libhal.a` 的上述 10 个函数没有提供对应的明确接口；对现有两库符号表的相关名称检索也未定位到 FM 搜台/RSSI 入口。这是当前证据范围内的缺口，不能据此判定芯片不支持这些功能，更不能用循环改频率冒充有效搜台。
