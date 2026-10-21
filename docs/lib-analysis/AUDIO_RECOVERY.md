# 音频输入、SDADC 与本地 DAC

本轮完成两类工作：从 `libhal.a` 恢复模拟输入和 SDADC 的 **18 个导出函数**；根据本仓库公开 BSP 实现片内 DAC 的 **48 kHz 双声道 16 bit 播放**。它们均操作硬件寄存器，不模拟音频串行时序。已完成离线测试，尚无实机录放结果。

## 实现与证据

| 范围 | 实现 | 验证 |
| --- | --- | --- |
| `audio.o`，6 个函数与 24 字节增益表 | [audio_recovered.c](../../bare/drivers/src/audio_recovered.c) | [2938 项原库差分](generated/recovery/audio-differential.json) |
| `sdadc.o`，12 个函数 | [sdadc_recovered.c](../../bare/drivers/src/sdadc_recovered.c) | [552 项原库差分](generated/recovery/sdadc-differential.json) |
| DAC/AUBUF 播放 | [dac_output.c](../../bare/drivers/src/dac_output.c) | 寄存器模型测试与 RV32 编译；基于公开 BSP，不属于原库差分 |
| 采集/播放封装 | [drv_adc.c](../../bare/drivers/src/drv_adc.c)、[drv_audio.c](../../bare/drivers/src/drv_audio.c) | 生命周期、DMA 参数、路由与部分写入测试，vendor/recovered 独立链接 |

`audio.o` 这个名字容易误导：它提供 `set_aux_analog_gain`、`set_mic_analog_gain`、`aux_analog_channel_select/close`、`sdadc_analog_mic_start/exit`，没有 PCM 播放、DAC 队列或 I2S 驱动。参数和通道编码见 [模拟层审计](generated/recovery/audio-notes.md)。

`sdadc.o` 的恢复保留原始错误处理、访问顺序、弱符号与段布局，包含零计数减一溢出等原库行为；面向应用的封装另加校验。旧 [分析 specimen](recovered/sdadc_recovered.c) 保留作历史记录，当前构建使用 `bare/drivers/src/sdadc_recovered.c`。

## DAC 播放接口

本地实现依据 [RT-Thread 音频驱动](../../rtt/bsp/board/ports/audio/drv_sound.c) 的 PLL、模拟输出及 AUBUF 路径。详细寄存器、数据手册引脚和公开代码问题见 [平台核查](AUDIO_PLATFORM_NOTES.md)。

`drv_audio_init()` 现在选择本地 DAC，成功后保持静音。格式固定为 48000 Hz、两个有符号 16 位样本组成一帧。下面是已经完成板级接入后的单次提交方式：

```c
#include "drv_audio.h"

/* 初始化一次后，持续供数的循环中： */
uint32_t accepted = drv_audio_write_frames(pcm, remaining_frames);
pcm += accepted * 2;
remaining_frames -= accepted;
/* 下次调用只提交剩余尾部；剩余为零时再加载下一块 PCM。 */
```

可独立编译的持续测试音入口是 [audio_play.c](../../bare/app/audio_play.c)，包括初始化、低音量和解除静音。没有完成平台钩子时，该示例返回失败。

一次调用只向当前有空间的 FIFO 写入，不等待播放；返回已接受帧数，可能为零或部分长度。已接受输入可立即复用，硬件使用独立的持久 1024 字节缓冲。低/高半字的排列有 BSP 依据，但物理左右声道对应关系仍需测试。`drv_audio_stop()` 静音并关闭数字输出，不执行 drain；下次启动需重新准备空 AUBUF。

旧 `drv_audio_write()` 的 bool 返回值无法表达部分消费，因此仍限定为旧 USB 音频接口，当前返回 false。本地 DAC 调用者必须迁移到 `drv_audio_write_frames()`。USB Audio 没有因本轮 DAC 播放实现而自动可用。

## 板级前提

DAC 的 weak `dac_output_platform_prepare()` 默认返回 false。真实实现需要：

- 确认 26 MHz 参考源、模拟输出及外部 VCM 接线，提供真实毫秒延时。
- 独占 PLL1、DAC 和 AUBUF0；启动前确保硬件缓冲为空/复位，保证缓存一致性。
- 将模块提供的持久缓冲映射到真实 DMA RAM。当前支持的整个缓冲窗口限定为 BSP 已知的 `[0x11000, 0x30000)`，这是实现范围而非推断的硬件地址位宽。
- 在 `dac_output_platform_release()` 中收回平台资源，并保证下次 prepare 可重新启动。通用驱动不擅自关闭共享 PLL 和模拟电源。

`bare/link.ld` 当前通用 RAM 起点是 `0x20000000`，不能直接当作该芯片可用 DMA 地址。仅令钩子返回 true 或对指针截断都不成立。此工作没有修改或验证完整固件启动/链接布局。

公开 BSP 的默认 DAC 配置和 PLL 族存在不一致：本实现明确设置 245.76 MHz PLL、除 10 再除 512 得 48 kHz，并同步 SDADC 用来判断 PLL 族的 `DACDIGCON0.bit1`。这有本地源代码/机器码依据，仍需测量实际采样频率。

## 采集接口与修正

原 `DRV_ADC_CH_MIC=0` 被直接传给原库，但原库拒绝零通道；现在映射到已证实的 MIC 单 bank 编码 `0x0c`。原 gain 是 `uint8_t`，现在改为 `uint16_t`：低 5 位为数字增益，模拟索引放在高位，封装只接受 0–23 的索引。

原 `sdadc_start(void)` 声明丢失 DMA 地址参数，实际机器码把 `a0` 写到 DMA 地址寄存器。新的使用顺序为：

1. `drv_adc_init(&cfg)`：先验证 MIC、采样率和增益，再由 `drv_adc_platform_init()` 确认共享时钟、模拟资源、真实延时和 IRQ 接入。
2. `drv_adc_set_dma_samples(nonzero_units)`：设置原库计数，拒绝零和运行期间修改。
3. `drv_adc_start_dma(buffer, capacity_bytes)`：明确提供持久缓冲及字节容量；`drv_adc_platform_dma()` 验证计数单位、地址映射、容量/格式、缓存一致性后返回总线地址。
4. 真正的 SDADC ISR 调用 `drv_adc_isr()`，通知原始 event ID。停止调用 `drv_adc_stop()`，会失效配置，重启必须重新初始化和设置计数。

默认平台钩子拒绝启动；旧无缓冲 `drv_adc_start()` 也返回 false。回调运行在 ISR 上下文，不能重入配置/启动/停止，平台需串行化这些访问并在停止时终止相关中断。示例 [mic_read.c](../../bare/app/mic_read.c) 已改用显式缓冲并检查每一步失败；它只输出事件计数，不把未知数据布局当作录音结果。

必须保留的边界：

- DMA 寄存器计数是配置值减一，但其实际样本/字节单位尚未确认；字节容量参数本身不能证明足够，板级验证不可省略。
- ISR 仅能确认状态 bit 1/0→event 1/2、bit 3/2→event 3/4，不能直接标注为半满/全满。
- 双 bank 启动路径只设置一个 DMA 地址，本封装不开放该模式。
- AUX start/exit 在现有 shim 中为空；即使模拟选择函数存在也不能证明完整 AUX 初始化，因此封装拒绝 AUX。
- `drv_adc_read()` 尚未实现同步 PCM 读取，仍返回零；没有伪造采样数据。
- DAC、FM 和 SDADC 共用资源，当前单一路径使用，不提供并发混音/同步录放。

## I2S 与后续范围

更新：已从另一份 AB53XX SDK 找到 `i2s.o`，恢复硬件 SRC 输出并接入 RT-Thread。
支持范围、兼容性依据及 136 组差分验证见 [I2S_RECOVERY.md](I2S_RECOVERY.md)。
以下“缺少 IIS 对象”的结论描述原本库集合的分析结果，不包含这次新增材料。

数据手册确实声明 IIS，公开头文件也有复用引脚和 IRQ 编号；已记录在平台核查文档。但本次查到的材料没有足够的 IIS 控制器寄存器、时序和 DMA 协议依据，不能将片内 DAC 播放称为 I2S 驱动。

下一步先完成真实 RAM/启动布局和平台时钟接入，用测试音确认采样频率、左右声道、满/欠载及停止行为；再验证单 bank MIC 的 DMA 格式、计数和事件。在此基础上加入 SD 分块连续供数、文件系统/WAV，以及有证据的 I2S 控制器移植。USB 音频仍是独立后续工作。

## 构建与验证

```sh
make -C bare AUDIO_BACKEND=recovered
```

默认 `vendor`，恢复选项同时替换 `audio.o` 与 `sdadc.o`，输出在 `bare/build/recovered-audio/`。SPI/FM/BT/USB/AUDIO 共 32 种后端组合通过选择检查。DAC/SD 的公开硬件实现不依赖恢复选项。

复跑命令（差分需要 Unicorn 2.1.4、pyelftools、clang、支持 RV32 的 LLD 及 LLVM objdump）：

```sh
python3 docs/lib-analysis/test_audio_recovery.py --linker /path/to/ld.lld
python3 docs/lib-analysis/test_sdadc_recovery.py --linker /path/to/ld.lld
python3 docs/lib-analysis/test_dac_output.py
python3 docs/lib-analysis/test_audio_integration.py --linker /path/to/ld.lld
```

3490 项差分比较原库机器码与恢复代码的 MMIO 顺序、状态、调用、返回及 callee-save 行为。模拟输入测试覆盖全部低字节通道编码与增益边界；SDADC 测试覆盖全部通道编码、采样率/PLL 两族、零计数与边界、启动/退出和中断组合。厂家自定义 restore 指令用 ABI shim，外部模拟/延时/回调在 SDADC 差分中使用钩子，不代表真实模拟电路运行。

集成检查保留 18 个原导出函数的名称、weak/global 绑定和代码段；两种独立链接都无未解析符号，恢复配置未拉入被替换对象。封装测试检查默认拒绝、参数校验、地址传递、DMA 生命周期、部分帧写入和资源路由；USB 集成测试亦通过。独立链接是人工地址布局，不能替代完整固件启动、DMA 总线和音频波形验证。
