# AB32VG1 MP3 硬件路径验证

2026-11-03 在 V2.0 开发板（ROM 标识 CRWN）验证了片上 AUDEC 的 MP3 解码运算路径。
软件前端与硬件协同工作；当前综合固件尚未集成完整的 MP3 文件播放器。

已将硬件流程整理为 `bsp/libraries/hal_drivers/drv_audec.c/.h`，通过
`BSP_USING_AUDEC` 选项注册 `audec0`。默认配置关闭此选项：现有蓝牙内存布局
与 AUDEC 工作区冲突，Kconfig 和编译期检查均禁止同时启用，尚未调整综合配置的内存布局。
这里的“蓝牙布局”指启用蓝牙时选择的 `link_bt_ram.lds`：它把部分 RTT 内核和
蓝牙辅助代码放在 `0x50000–0x579ff`，与 AUDEC 所用的 `0x54000–0x56fff` 重叠。
这不是蓝牙控制器的 `0x11000–0x137ff` 保留区，也不表示两种硬件不能共存；
需要先迁移重叠代码并重新核算 SRAM/堆预算，再解除配置限制和进行共存测试。

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

`DECODE` 输入为 576 个量化频谱字、39 个频带描述字以及 `block_type=0..3`，
格式见头文件和已验证的测试向量。当前仅开放 MPEG-1 Layer III、44.1 kHz、单声道
输入，不接收任意寄存器控制值，也不接受原始 MP3 文件字节。输出缓冲区四字节对齐，
至少 2,304 字节，内容为交错的有符号 16 位 L/R PCM。

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
python3 tools/rtt/check_builds.py default audec audec_fm
```

## 实测结果

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
  写入，单声道频带参数从 `0x56b00` 写入。是否支持工作区重定位尚未验证。
- 恢复出的控制、启动、状态和 PCM 地址寄存器分别位于 `0x180`、`0x184`、
  `0x188`、`0x190`。控制 bit 26 选择 RAM 输出，bit 27 用于复位；
  测试将 AUDEC 时钟设为系统时钟的四分之一，退出后恢复原时钟配置。
- 状态 `0x0f` 只表示前面阶段完成，此时 PCM 尚未写完。
  测试等待状态 bit 5，设置 200 ms 超时，并检查输出完整性和边界。
- 本板执行标准 RISC-V `FENCE` 会触发非法指令异常。此测试的代码、输入、
  输出均在无数据缓存的 SRAM，使用 volatile MMIO 和编译器内存屏障。
- 当前综合固件的内核代码占用了上述工作区。测试因此使用 RTT 工程的临时配置，
  将内核放在主 SRAM，禁用蓝牙、FM、USB、TF、IR 和电源管理，保留串口、按键、
  红灯及 RTC。测试配置不是新的独立产品项目。

测试结束后已重新写入并通过 CRC 验证恢复原综合固件。恢复后 RTT 5.2.2 正常启动，
堆总量 23,272 字节、已用 9,376、可用 13,896；红灯 `active=0`、`error=0`。

目前没有验证其他码率、采样率、双声道 MP3、bit reservoir 或与 FM/蓝牙同时运行。
完整接入还需要恢复板上的流解析和 Huffman 前端、规划工作 RAM、接入文件输入与
`sound0` 播放，以及验证资源互斥和共存。此次没有进行耳机试听，结论来自 PCM 数值比较。

## 复现

在仓库根目录执行，依赖现有交叉工具链、SCons 和 FFmpeg：

```sh
python3 tools/rtt/test_mp3_hw.py build
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

如需重新生成输入向量，另准备指定版本的 SDK 库，并在 Python 环境安装
`unicorn==2.1.4`、`pyelftools`：

```sh
python3 tools/research/mp3_vectors.py --archive /path/to/libcodecs.a --output /tmp/mp3-vectors
```

生成器校验 SDK SHA-256，只在电脑端执行库代码。本次重新生成的样本和向量与
实测输入逐字节一致；不同 FFmpeg/LAME 版本可能生成不同的 MP3，应重新建立对应记录。
