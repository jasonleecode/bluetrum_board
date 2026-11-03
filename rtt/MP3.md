# AB32VG1 MP3 硬件路径验证

2026-11-03 在 V2.0 开发板（ROM 标识 CRWN）验证了片上 AUDEC 的 MP3 解码运算路径。
软件前端与硬件协同工作；当前综合固件尚未集成完整的 MP3 文件播放器。

已将硬件流程整理为 `bsp/libraries/hal_drivers/drv_audec.c/.h`，通过
`BSP_USING_AUDEC` 选项注册 `audec0`，默认综合配置已启用。
`link_bt_ram.lds` 已迁移原先与 AUDEC 重叠的内核和 HCI 代码，并解除与蓝牙的配置互斥：

| 地址（含首尾） | 用途 |
| --- | --- |
| `0x11000–0x137ff` | 蓝牙控制器固定 ABI 区，保持不变 |
| `0x13800–0x2efff` | 主 SRAM：其余常驻代码、数据、堆和 AUDEC 输出 DMA 缓冲 |
| `0x2f000–0x2ffff` | 原有 4 KiB 启动/IRQ 栈 |
| `0x50000–0x53fff` | 常驻内核代码 |
| `0x54000–0x56fff` | AUDEC 固定 12 KiB 工作区，禁止放代码或堆 |
| `0x57000–0x579ff` | 常驻小内存分配器，由启动代码单独复制 |

中断、临界区、Flash 缓存填充和恢复的蓝牙底层仍在 SRAM；音频生命周期、FM 线程入口、
蓝牙线程控制及已审核的 libc 调用放入缓存 Flash。AUDEC 的关中断辅助函数使用 `noinline`，
避免编译器把它们内联到 Flash 中。链接断言和 `memory_report.py` 检查区间、复制符号及关键函数地址。
综合布局即使关闭 AUDEC，也保留这 12 KiB，避免后续开关功能时再次产生重叠。


## HAL 驱动接口

使用 `rt_device_find("audec0")` 查找设备，再调用 `rt_device_control()`。
所有调用只能在线程上下文进行；不需要先调用 `rt_device_open()`。

| 控制命令 | 参数 | 行为 |
| --- | --- | --- |
| `AB32_AUDEC_BEGIN` | `RT_NULL` | 当前线程独占解码会话，保存时钟配置，清除历史并启动工作时钟 |
| `AB32_AUDEC_DECODE` | `struct ab32_audec_granule *` | 同步处理一个 granule，成功时输出 576 个双声道 PCM 采样字 |
| `AB32_AUDEC_RESET` | `RT_NULL` | 清除解码历史及故障状态，保持会话所有权 |
| `AB32_AUDEC_END` | `RT_NULL` | 复位并停止硬件，恢复所占用的时钟字段，释放会话 |
| `AB32_AUDEC_GET_STATUS` | `struct ab32_audec_status *` | 获取工作区、运行状态、成功/超时/错误计数及最近硬件状态 |

每个输入声道提供 576 个量化频谱字、39 个频带描述字，以及独立的 `block_type=0..3`。
当前支持 MPEG-1 Layer III、44.1 kHz，格式见头文件和已验证向量：

| `mode` | 输入 | 硬件行为 |
| --- | --- | --- |
| `AB32_AUDEC_MONO`（0，默认） | `spectrum`、`bands`、`block_type` | 单声道复制到 L/R |
| `AB32_AUDEC_STEREO` | 上述左声道 + `spectrum_right`、`bands_right`、`block_type_right` | 独立 L/R |
| `AB32_AUDEC_MS_STEREO` | 两组 M/S 频谱与频带输入 | 硬件 M/S 联合立体声处理后输出 L/R |

输入和输出均四字节对齐。单声道请求的右侧指针必须为 `NULL`，右侧块类型必须为零。
输出始终是 576 对交错的有符号 16 位 L/R PCM，缓冲至少 2,304 字节。
不接受任意寄存器控制值、原始 MP3 文件字节或 intensity stereo 模式。

将整个请求结构零初始化即可保留旧单声道用法。双声道提交例如：

```c
struct ab32_audec_granule g = {
    .mode = AB32_AUDEC_STEREO,
    .spectrum = left_spectrum, .bands = left_bands, .block_type = left_block,
    .spectrum_right = right_spectrum, .bands_right = right_bands,
    .block_type_right = right_block,
    .pcm = output, .pcm_bytes = 2304,
};
rt_device_control(dev, AB32_AUDEC_DECODE, &g); /* 在 BEGIN 后调用 */
```

同一流中可在独立立体声与 M/S 模式间切换；单/双声道数量变化必须先 `RESET` 或重新 `BEGIN`，
避免复用不匹配的历史。状态中的 `input_channels` 在复位后为 0，成功解码后为 1 或 2。

调用顺序为 `BEGIN → DECODE × N → END`；后续 granule 保留硬件历史，
开始新流或故障后继续时执行 `RESET`。申请会话的线程必须在退出前执行 `END`。
其他线程的解码、复位或释放操作返回 `-RT_EBUSY`，不会打断当前流。

默认超时 200 ms，可指定 1–1,000 ms。驱动等待最后阶段完成，使用带保护字的
内部 DMA 缓冲区，成功后才复制到调用方；超时或边界错误时停止硬件并进入故障状态，
调用方输出保持不变。`RESET` 或 `END` 可解除故障。输入输出不能与工作区或内部
DMA 缓冲区重叠。驱动没有新增工作线程或动态内存分配。

启用选项会自动使用 SRAM 链接布局，通过 `.audec_work (NOLOAD)` 预留固定工作区；
旧的自定义链接脚本若没有正确放置该区，初始化会在访问外设前失败。
电源驱动增加 `blockers=20`：会话未释放时禁止休眠/软关机。
AUDEC 与 FM 的编译组合已通过；同时运行和与完整播放器的资源协调仍待验证。

主机控制流测试覆盖非法参数、竞争线程、错误内存布局、分阶段完成、tick 回绕、
超时后复位、DMA 边界错误，以及共享时钟字段恢复；这些测试不模拟 MP3 数学运算：

```sh
python3 tools/rtt/test_audec.py
python3 tools/rtt/test_rtc.py
python3 tools/rtt/check_builds.py default audec_integrated audec audec_fm
```

## 首次单声道实测结果

输入是自行生成的 1 kHz 正弦波 MP3：44.1 kHz、单声道、MPEG-1 Layer III、
128 kbit/s，关闭 bit reservoir 和 Xing/ID3 标签。8 帧共 16 个 granule，
产生 9,216 个单声道采样，硬件复制到左右两个 16 位输出声道。

两次独立运行均通过，输出 PCM 的 SHA-256 完全相同。与 FFmpeg 软件解码逐点比较，
不做移位、增益拟合或裁掉首尾：

| 检查 | 结果 |
| --- | --- |
| 最大绝对误差 | 1 LSB（16 位 PCM） |
| 均方根误差 | 0.11553 LSB |
| 相关系数 | 0.99999999885 |
| 完成状态 | 每个 granule 最终为 `0x3f` |
| DMA 输出 | 每次完整改写 576 个 32 位立体声采样字 |
| 输出缓冲区前后保护字 | 两轮全部完好 |
| 重复运行 | 逐字节相同 |

原始寄存器验证结果见 [实机报告](../docs/lib-analysis/generated/recovery/mp3-hardware.json)，
对应程序保留为 `tools/rtt/tests/mp3/register_probe.c`。
原始串口记录以 `mp3-hardware.log.zlib` 和 `mp3-hardware-repeat.log.zlib`
保存在同目录，可用 Python `zlib.decompress()` 解压后重新比较。

HAL 驱动另外完成两轮实板验证：通过 `audec0` 提交全部 16 个 granule，
两轮 PCM 均与原始寄存器验证逐字节相同，最大参考误差仍为 1 LSB。
见 [驱动实机报告](../docs/lib-analysis/generated/recovery/audec-driver-hardware.json)，
对应完整串口记录为同目录的 `audec-driver-uart.log.zlib`。

## 如何区分硬件解码与软件播放

电脑端用 Unicorn 执行同系列 SDK `libcodecs.a` 中的 `layer3.o`、`bit.o`、
第一个 `huffman.o`，完成 side-info、scalefactor 和 Huffman 等软件前端处理，
记录每次提交硬件前的量化频谱与频带参数。这些输入不是 PCM。

板上只运行本仓库的 C 驱动和测试程序，将输入写入工作区并启动 AUDEC；硬件将结果
写入另一个 RAM 缓冲区，再通过 UART 回传电脑。板上不链接、不执行原厂 MP3 库，
也不包含 FFmpeg 的参考 PCM。数值对比因此验证了真实硬件运算，而不仅是寄存器响应。

所用 SDK 固定于
[aa0fc8f / libcodecs.a](https://github.com/ZhiqingLi/Sdk_Refresh/blob/aa0fc8f993bd0c2a6982bf0a95b3461ad7e7aa6d/AB53XX_SDK/app/platform/libs/libcodecs.a)，
SHA-256、样本和输入数据格式见 [manifest.json](../tools/rtt/tests/mp3/manifest.json)。
库仅用于电脑端分析，没有加入仓库或综合固件依赖。

## 已确认的接口与限制

- 当前路径使用 `0x54000–0x56fff` 共 12 KiB 工作区；量化频谱从 `0x55000`
  写入，左频带参数从 `0x56b00` 写入；右频谱位于 `0x55d80`，右频带位于 `0x56b9c`。
  控制位 `[9:8]` 选择声道模式、`[11:10]` 选择联合立体声扩展、
  `[13:12]`/`[16:15]` 分别选择左右块类型。是否支持工作区重定位尚未验证。
- 恢复出的控制、启动、状态和 PCM 地址寄存器分别位于 `0x180`、`0x184`、
  `0x188`、`0x190`。控制 bit 26 选择 RAM 输出，bit 27 用于复位；
  测试将 AUDEC 时钟设为系统时钟的四分之一，退出后恢复原时钟配置。
- 状态 `0x0f` 只表示前面阶段完成，此时 PCM 尚未写完。
  测试等待状态 bit 5，设置 200 ms 超时，并检查输出完整性和边界。
- 本板执行标准 RISC-V `FENCE` 会触发非法指令异常。硬件工作区与 DMA 输出均在无数据缓存的
  SRAM，使用 volatile MMIO 和编译器内存屏障；Flash 中的线程代码/向量仅在 IRQ 开启时访问。
- 首次寄存器测试时，综合固件的内核代码曾占用上述工作区，因此当时使用 RTT 工程临时配置，
  将内核放在主 SRAM，禁用蓝牙、FM、USB、TF、IR 和电源管理，保留串口、按键、
  红灯及 RTC。测试配置不是新的独立产品项目。

首次测试结束后恢复了当时的综合固件。该历史版本 RTT 5.2.2 正常启动，
堆总量 23,272 字节、已用 9,376、可用 13,896；红灯 `active=0`、`error=0`。

目前没有验证其他码率/采样率、MPEG-2/2.5、intensity stereo、mixed blocks、bit reservoir
或与 FM/蓝牙同时运行。双声道与 M/S 的后续结果见下节。
完整接入还需要恢复板上的流解析和 Huffman 前端、接入文件输入与
`sound0` 播放，以及验证资源互斥和共存。此次没有进行耳机试听，结论来自 PCM 数值比较。


## 综合布局迁移后的回归

2026-11-03 已将默认综合配置的重叠代码迁移，并烧录验证。
测试副本保留 FM、NimBLE 和所有默认外设，只额外加入 Flash 中的测试向量及 `mp3hw` 命令；
输入/输出临时缓冲在命令运行时申请，结束后释放。

- 16 个 granule 全部完成，PCM SHA-256 与先前独立驱动测试一致，最大误差 1 LSB。
- 同一镜像完成硬解码后，连续三次 RTC 浅休眠恢复通过。
- 随后启动 NimBLE，三轮连接、GATT 1/20/60 字节读写/通知及重连通过；HCI errors 与 Host resets 均为零，堆用量不增长。
- 正式固件移除测试命令与向量，确认 `audec0` 已注册；FM 97.4 MHz 静音启停、静音 PCM 播放通过。
- 最后已复位清除 PCM 测试保留的缓冲，正式固件正常运行，红灯关闭。堆总量 19,176 字节，已用 9,376，可用 9,800。

尝试把内部 DMA 输出缓冲放到 `0x57000` 时，实板最终阶段停在 `0x1f` 并超时；
最终方案把输出缓冲留在主 SRAM，将 SRAM 尾部用作内核分配器代码。
代码、工作区和堆的地址检查与实测数据见 [内存记录](MEMORY.md) 和
[audec-integrated-hardware.json](../docs/lib-analysis/generated/recovery/audec-integrated-hardware.json)。

上述为同一综合镜像中的顺序回归，未验证硬解码与 BLE 同时运行，也不代表完整 MP3 文件播放已接入。
默认原始堆相对迁移前减少 4 KiB；完整 PCM 播放池和 NimBLE 不能同时保留，后续播放器需继续规划缓冲。


## 双声道与 M/S 扩展实测

驱动已加入独立右声道输入和 M/S 联合立体声模式。三种格式在同一综合测试镜像内
依次运行，每组 8 帧、16 个 granule、每声道 9,216 个采样。全部完成，保护字完好；
逐采样比较没有做时移、增益拟合或首尾裁剪：

| 测试输入（44.1 kHz / 128 kbit/s） | L 最大误差 / RMS | R 最大误差 / RMS |
| --- | --- | --- |
| 原单声道 1 kHz | 1 / 0.11553 LSB | 1 / 0.11553 LSB |
| 独立立体声：L 997 Hz，R 1501 Hz | 1 / 0.09259 LSB | 1 / 0.07439 LSB |
| M/S：两侧含 997 Hz，R 额外含 1501 Hz | 1 / 0.10099 LSB | 1 / 0.08653 LSB |

单声道结果与旧驱动逐字节一致；两组立体声左右输出均不同，且各自与 FFmpeg 对应声道匹配。
模型测试还覆盖不完整/越界/未对齐的右声道输入、独立左右块类型、模式切换及声道数量变化时要求复位。
已通过默认综合、独立 AUDEC、AUDEC+FM、AUDEC 综合四种编译配置。

扩展后驱动的 12 KiB 工作区、2312 字节私有 DMA 缓冲和默认原始堆 19264 字节均不变。
双声道调用方需要额外提供右侧 2460 字节输入；测试命令按需分配并释放，三组测试前后已用堆均为 9376。
这不消除完整播放器的软件前端、文件缓冲和 PCM 播放池预算限制。

证据见 [audec-stereo-hardware.json](../docs/lib-analysis/generated/recovery/audec-stereo-hardware.json)，
原始日志为同目录 `audec-stereo-uart.log.zlib`。样本与向量在
`tools/rtt/tests/mp3/stereo/` 和 `joint_stereo/`，由恢复工具在电脑生成，板上没有链接原厂库。
本轮验证回传的数值 PCM，未进行耳机听感或连续文件播放测试。

## 复现

在仓库根目录执行，依赖现有交叉工具链、SCons 和 FFmpeg：

```sh
python3 tools/rtt/test_mp3_hw.py build
# 保留默认综合配置（包括 NimBLE），验证新的内存布局：
python3 tools/rtt/test_mp3_hw.py build --integrated
# 同一镜像包含三组向量：
python3 tools/rtt/test_mp3_hw.py build --integrated --profile all
```

当前测试程序通过 `audec0` 调用 HAL 驱动，不再直接访问寄存器。
脚本只在 `/tmp/ab32-mp3-probe-*` 创建启用 `BSP_USING_AUDEC` 的副本并输出 `probe.dcf`，不会修改默认配置、
访问串口或自动烧录。先保留当前综合固件，再将输出路径代入：

```sh
tools/linux/downloader flash /tmp/ab32-mp3-probe-XXX/probe.dcf --port /dev/ttyUSB0
python3 tools/rtt/console.py --port /dev/ttyUSB0 --wait 12 mp3hw > /tmp/mp3-uart.log
python3 tools/rtt/test_mp3_hw.py compare /tmp/mp3-uart.log --output /tmp/mp3-result.json
tools/linux/downloader flash rtt/bsp/rtthread.dcf --port /dev/ttyUSB0
```

三组镜像的 shell 命令为 `mp3hw mono`、`mp3hw stereo`、`mp3hw joint_stereo`。
比较时指定对应 `--profile`，可从包含三轮结果的日志中选取相应记录：

```sh
python3 tools/rtt/test_mp3_hw.py compare /tmp/mp3-uart.log --profile stereo
python3 tools/rtt/test_mp3_hw.py compare /tmp/mp3-uart.log --profile joint_stereo
```

如需重新生成输入向量，另准备指定版本的 SDK 库，并在 Python 环境安装
`unicorn==2.1.4`、`pyelftools`：

```sh
python3 tools/research/mp3_vectors.py --archive /path/to/libcodecs.a --output /tmp/mp3-vectors
# 可选 --profile stereo 或 --profile joint_stereo
```

生成器校验 SDK SHA-256，只在电脑端执行库代码。本次重新生成的样本和向量与
实测输入逐字节一致；不同 FFmpeg/LAME 版本可能生成不同的 MP3，应重新建立对应记录。
