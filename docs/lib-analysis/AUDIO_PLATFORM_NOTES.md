# 音频硬件平台核查与 DAC 输出

本轮把“片内 DAC 播放”“音频 SDADC 采集”“SARADC 电压测量”和“IIS 数字音频接口”分开处理。证据来自本仓库公开 BSP、芯片资料和 `libhal.a` 的 SDADC 恢复结果，没有接板测量。DAC 新模块是按公开 BSP 实现的硬件驱动，不计入原库函数差分恢复数量。

## 证据入口

| 文件/位置 | 能确认的内容 |
| --- | --- |
| [drv_sound.c](../../rtt/bsp/board/ports/audio/drv_sound.c)，37–62 行 | `adpll_init`，26 MHz 参考、PLL1 供电、分频与三次 1 ms 延时 |
| 同文件，64–110 行 | `dac_start`，模拟电源、外部 VCM、单端 PA、双 DAC 使能 |
| 同文件，123–165 行 | DAC 采样率选择、单双声道、数字音量 |
| 同文件，362–415 行 | 1024 字节 AUBUF RAM、计数写法、满标志和 32 位写入 |
| 同文件，448–459 行 | AUBUF 中断 pending/清除/使能位；它不是一次整缓冲播放完毕的证明 |
| [ab32vg1.h](../../rtt/bsp/libraries/hal_libraries/bmsis/include/ab32vg1.h)，138–175 行 | 寄存器实际地址；24–32 行为 AUBUF/SDADC/I2S 中断编号 |
| [ab32vgx.h](../../rtt/bsp/libraries/hal_libraries/bmsis/include/ab32vgx.h)，20 行 | 此 BSP 的 `DMA_ADR(x)` 仅为 32 位强转 |
| [link.lds](../../rtt/bsp/link.lds)，4–31 行 | 可写 RAM 从 `0x11000` 开始，bram/heap/data/stack/comm 合计至 `0x30000`；另有标为 rx 的 ram1 |
| [DataSheet](../ab32vg1_doc/AB32VG1_DataSheet.pdf)，印刷页 4、6–9 | 16 bit 双声道 DAC/ADC、IIS 能力与封装引脚 |
| [V02 原理图](../ab32vg1_doc/AB32VG1_Prougen_Schematic_V02.pdf) | DACR/DACL 接 VOUTRP/VOUTLP；VCM、VDDDAC 各有 105 电容；不能因此假定用户当前板的连接一致 |
| [sdadc_recovered.c](recovered/sdadc_recovered.c)，115–153、197–212 行 | 历史分析 specimen 中的 DMA 地址、计数、ISR 事件机器级行为 |

`ab32vg1_hal_dac.c` 只有空 weak `hal_dac_mspinit`，本身没有可用 DAC 驱动。实际播放依据是 `board/ports/audio/drv_sound.c`，其 `sound0` 只注册输出/混音能力。变量叫 `SAI` 或函数叫 `saia_*` 不代表这里通过 IIS/I2S 输出。

## DAC 寄存器与缓冲约定

| 地址 | BSP 名称 | 已确认用法 |
| --- | --- | --- |
| `0x104` | AUBUFDATA | 每次写 32 位 PCM 字；双声道 16 bit 下为一帧 |
| `0x108` | AUBUFCON | bit 8：满；bit 5：pending；bit 1：清 pending；bit 4：中断使能 |
| `0x10c` | AUBUFSTARTADDR | AUBUF 持久 RAM 的 DMA 地址，不是每次传入的用户 PCM 指针 |
| `0x110` | AUBUFSIZE | 1024 字节时 `(1024/4 - 1) | ((1024/8)<<16)` = `0x008000ff` |
| `0x140` | DACDIGCON0 | BSP 设置 bit 0/10 开始；bits 5:2 的 0 表示 48 kHz；bit 6 置位；双声道清 bit 7/8 |
| `0x144` | DACVOLCON | `percent * 327 | (2<<16)`，percent 限 0–100 |
| `0x1f0` | AUANGCON0 | 模拟偏置/LDO/VCM/DAC/PA；需匹配外部 VCM 与单端输出连接 |
| `0x1f4` | AUANGCON1 | bit 0/1：双 DAC；bit 3：PA mute；bit 12：LPF→PA |
| `0x1fc` | AUANGCON3 | 片内 PA 增益字段：bits 6:4、3:0、11:8、14:12 |
| `0x390/0x39c/0x3a8` | PLL1DIV/PLL1CON/CLKCON2 | 48 kHz 时分频字 `0x973ca`，audio 分频选择 `0x90` |

`AUBUFSIZE` 的两个字段取值直接照 BSP；没有把高半字的阈值解释为“半缓冲完成”。`AUBUFFIFOCNT` 虽有地址定义，本轮没有依赖其未公开的计数语义。满标志轮询足以实现非阻塞部分写入，但返回入队帧数不等于这些帧已从模拟引脚播放完。

32 位字由两个有符号 16 bit 样本组成，第一个样本放低 16 位，第二个放高 16 位。BSP 直接从内存读取 32 位字支持这一字节顺序；哪一个半字对应物理 L/R 尚待独立左右声道测试。`sddac_test.c` 的数组注释虽写 stereo，测试只循环写一段波形，不能单凭该注释证明左右声道顺序。

公开 `Register.pdf` 没有 AUBUF/DAC/SDADC 章节。SD0DMAADR 的 21 位地址说明只适用于 SD，不能用来证明音频 DMA 的地址位宽。本轮 DAC 为避免把错误的 `bare/link.ld` 中 `0x20000000` 直接写入硬件，保守要求整个 1024 字节映射位于 BSP 已知的 `[0x11000,0x30000)`；这是当前支持范围限制，不是寄存器位宽结论。平台仍须确认映射对应真实、独占、可被 DMA 访问的 RAM。

## 新增 DAC 接口

代码：[dac_output.h](../../bare/drivers/inc/dac_output.h)、[dac_output.c](../../bare/drivers/src/dac_output.c)。

- `dac_output_init()` 固定 48000 Hz、双声道、16 bit；分配静态对齐的 1024 字节硬件缓冲。成功后音量为 55%，PA 保持静音。
- `dac_output_write(pcm, frame_count)` 每次写前检查满标志；返回实际入队的帧数。一个 frame 是两个 `int16_t`、4 字节。调用者继续提交剩余帧；函数不等待 DMA 或播放完成。
- `dac_output_set_volume(0..100)` 和 `dac_output_mute(bool)` 控制本地 DAC；与 USB 音频没有依赖关系。
- `dac_output_stop()` 先静音，关闭 DAC 数字输出位和 AUBUF 中断，然后调用平台 release。它会中止队列，不提供 drain。共享 PLL 和模拟电源不由通用模块关闭。

`dac_output_platform_prepare(buffer, bytes, &platform)` 的 weak 默认实现返回 false，且不会操作寄存器。真实板级覆盖必须确认：26 MHz 参考源与时钟、模拟输出和 VCM 连接、AUBUF 已空/复位、DMA RAM 映射/缓存一致性，并独占 PLL1/DAC/AUBUF0。返回真实 `delay_ms` 回调和 DMA 地址。若返回设置无效，模块在任何 MMIO 前调用 release 并失败。

所有调用须串行。这个起步接口不支持与 FM DMA、SDADC、USB 音频同时争用共享音频资源。三次 1 ms 等待来自 BSP，但不代表模拟上电、负载、音质已实测。stop 后由板级 release 收回资源，下次 prepare 应再次保证空 FIFO。

48 kHz 初始化还置 `DACDIGCON0.bit1`，使它与新写入的 PLL 族一致。这一点来自 `sdadc.o` 的实际行为：48 kHz 族设置 `PLL1DIV=0x973ca` 后置 bit 1，44.1 kHz 族设置 `0x98d80` 后清 bit 1；SDADC 之后通过该位决定是否重配共享 PLL。公开 `drv_sound.c` 未同步此位，也在 `sound_init` 中固定调用 `adpll_init(0)` 却默认配置 48 kHz，因此不能不加核查地整段复制其初始化。

## 音频 ADC 与 SARADC

`docs/ab32vg1_doc/test_program/adc_test.c` 使用 `adc0`，对应 `hal_adc`/SARADC 电压测量。它的 `1<<10` 与 datasheet 的 10 bit SARADC 一致，注释“12位”不准确。该例不提供音频 SDADC DMA 缓冲单位、半满完成语义或麦克风流式录音证据。

历史裸机封装存在几处确定错误，集成时应修正：

- 原 `DRV_ADC_CH_MIC=0` 会被 `sdadc_set_channel(0)` 拒绝。SDADC channel 是编码选择，低/高半字节对应两个 bank；`0x0c/0x0d` 与 `0xc0/0xd0` 进入 MIC 路径。不能把编码直接叫作已验证物理引脚。
- 原 `sdadc_start(void)` 丢失 DMA 缓冲地址参数；实际函数把传入 a0 写到 `0x1cc` 或 `0x1d4`。
- DMA 计数写到 `0x1d0`/`0x1d8` 的值是配置数减一。零会下溢，单位和实际占用长度仍需单独验证。
- 两 bank 模式只设置第一个 DMA 地址，不能假定一次传一个双声道缓冲就安全。
- gain 是 16 位 packed 字段：低 5 位为数字增益，右移 5 后的字节进入模拟增益函数；原封装 `uint8_t gain` 丢失高位信息。
- `sdadc_isr` 的 status `0x1c8` bit 1/0 对应 event 1/2，bit 3/2 对应 event 3/4；清除写 `0x1c4`。这里只证实事件编号及优先级，未证实“半/满缓冲”含义。
- 原 `sdadc_analog_aux_start/exit` 在 shim 中为空，返回成功不能证明 AUX 模拟通路建立。后续 AUX 使能必须有真实平台实现。

建议 SDADC 首先只接单 bank、显式缓冲地址和容量、严格非零计数，并让平台确认 DMA 计数单位/映射及 IRQ 接入；在这些事实未确认前拒绝启动。原始回调事件可以原样提供，不能伪装为已完成多少字节的读 API。

## IIS/I2S 的边界

芯片确实列出 IIS，且有 `IRQ_I2S_VECTOR=17`、`RCU_IIS` 枚举和 GPIO_AFCON2 注释。datasheet 列出两组复用引脚：

| 信号 | G1 | G2 |
| --- | --- | --- |
| MCLK | PA4，封装 32 | PE4，封装 3 |
| LRCLK | PA3，封装 33 | PE3，封装 4 |
| SCLK | PA2，封装 34 | PE2，封装 5 |
| DO | PA1，封装 35 | PE1，封装 6 |
| DI | PA0，封装 36 | PE0，封装 7 |

这说明硬件能力和可选 pinmux，不足以实现控制器：当前审计范围内没有 IIS 时序/寄存器布局、主从配置、字长格式、DMA 传输协议的可确认驱动。因此本轮不新增猜测的 I2S 收发 API。

片内模拟引脚为 MICL/PF2（37）、MICR（40）、DACR（41）、DACL（42）、VDDDAC（43）、VCM（44）、AGND（45）。它们与 IIS 引脚不同；DAC 播放无需通过 GPIO 模拟串行音频。

## 验证

运行 `python3 docs/lib-analysis/test_dac_output.py`。测试覆盖默认平台拒绝且无 MMIO、地址对齐/窗口/完整容量检查、缺延时回调、48 kHz PLL/模拟/DAC 配置、持久缓冲清零、按帧打包、FIFO 满与部分写、音量边界、静音、重复初始化、停止/重启，并编译 freestanding RV32IMAC 对象。

这些是可重复的寄存器模型测试和交叉编译，不是 DMA 实机传输、48 kHz 波形测量或音质测试。下一步板级验证应先校准 PLL/采样频率，再分别输出左右测试音，确认 FIFO 满/欠载行为、静音与停止，再接入 SD 分块读和连续供数。
