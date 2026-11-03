# 综合 RTT 内存记录（2026-11-03）

当前默认综合配置包括 FM、蓝牙/NimBLE、USB CDC、TF/FAT、红外、按键、RTC、电源管理、AUDEC 与 SPI1。
最新数据见“SPI1 接入”；后续章节保留各阶段历史用量。

## SPI1 接入

SPI1 驱动及 RTT 同步框架的线程路径放入 Flash，注册从 board-init 移到 device-init，
不会在启动关中断阶段执行 Flash 代码。GPIO 复用临界区、测试引脚恢复及 PM 忙状态
观察函数仍在 SRAM，`memory_report.py` 检查对应归属。

相比双声道 AUDEC 基线，`.comm` 从 77024 到 77304，`.bss` 从 16352 到 16480，
计入对齐后原始堆从 **19264 降为 18848 字节**，减少 416 字节，仍满足 18 KiB 下限。
`.flash` 为 133632 字节；`.ram1`、`.ram2` 和 AUDEC 固定工作区不变。
SPI1 总线静态注册，无专用线程、DMA 池或长期测试缓冲。

实板重复收发和 RTC 休眠恢复后，堆 total=18760、used=9376、available=9384，
与测试前相同；串口记录见 [spi-rtt-hardware.json](../docs/lib-analysis/generated/recovery/spi-rtt-hardware.json)。
这不是蓝牙和完整 PCM 池同时运行的验证，原有两者不能同时保留的限制仍在。


## AUDEC 工作区隔离

此前 `.ram1` 占用 `0x50000–0x579f7`，覆盖 AUDEC 固定工作区。
现在将 `0x54000–0x56fff` 完整保留为 12 KiB NOLOAD 区，综合固件默认启用 `audec0`。
HCI 代码/描述表、内核定时器和格式化等仍在 SRAM，只迁移存放地址；
中断、缓存填充、蓝牙 ABI 固定区和各线程/IRQ 栈大小均保持不变。

原先放在 SRAM 的 PCM 生命周期、FM 线程入口、蓝牙线程入口/命令控制和部分 libc
字符串/解析/64 位除法函数转到 Flash。蓝牙 `pump`、回调、恢复的控制器源码和共享描述表保持常驻。
启动阶段使用的 `rt_device_open/close` 也保持常驻，不能因其名称像普通设备 API 就移到 Flash。
AUDEC 的关中断辅助函数显式 `noinline`，避免从 SRAM 内联到 Flash 调用者。

| 段 | 迁移前（不含 AUDEC） | 迁移后默认固件 | 存放位置/说明 |
| --- | ---: | ---: | --- |
| `.comm` | 75376 | 77024 | 主 SRAM 常驻代码及初始化数据 |
| `.ram1` | 31224 | 16288 / 16384 | `0x50000` 起，余 96 字节 |
| `.ram2` | — | 2408 / 2560 | `0x57000` 起的小内存分配器，启动时从 Flash 复制 |
| `.audec_work` | — | 12288 | `0x54000` 起，独立固定工作区 |
| `.bss` | 13888 | 16352 | 含 2312 字节的 AUDEC 私有 DMA 缓冲和保护字 |
| `.bram` | 6540 / 10240 | 6540 / 10240 | 蓝牙固定 ABI 区不变 |
| `.heap` | 23360 | 19264 | 原始堆区，含分配器元数据 |
| `.stack` | 4096 | 4096 | 启动/IRQ 栈不变 |
| `.flash` | 116736 | 129536 | 缓存 Flash 中的线程路径 |

硬解码输出缓冲保留在主 SRAM。尝试将它放入 `0x57000` 时，实板状态停在 `0x1f` 并超时；
移回主 SRAM 后 16 个 granule 全部完成，故 SRAM 尾部用于 CPU 代码。
尾部代码增加独立的启动复制符号；另两种非 BT 链接布局将该复制长度设为零。

原始堆净减少 **4096 字节**。链接预算相应比之前少 4 KiB，默认至少 18 KiB；
并未通过缩减线程栈或 PCM 播放池获取空间。已验证单独的 BLE/GATT、PCM 和硬解码路径；
**不能同时保留完整 PCM 播放池和启动 NimBLE**，完整 MP3 前端/文件缓冲还需要另行预算。
USB/TF 同时运行的动态内存也未包含在这项保证内。

在保留全部外设的测试镜像中，完成硬解码后再启动 NimBLE，三轮 GATT 后已用堆仍为 15528 字节，
可用 3616 字节；硬解码测试临时缓冲已释放，三次 RTC 浅休眠恢复也通过。
正式固件启动后的堆总量为 19176、已用 9376、可用 9800 字节；静音 PCM 测试后已用 17872、可用 1304。
测试结束已复位，释放测试保留的 PCM 池。测试镜像有额外命令表，因此堆总量比正式镜像少 32 字节。
这些是顺序回归，不是同时播放 MP3 与蓝牙连接的验证。

`memory_report.py` 检查所有分配段与工作区不重叠、SRAM 代码复制地址/长度、关键函数归属及堆下限。
默认、AUDEC 综合、独立 AUDEC、AUDEC+FM、控制器、NimBLE、GPIO IRQ 和旧音频布局均已编译通过。
硬件与链接记录见 [audec-integrated-hardware.json](../docs/lib-analysis/generated/recovery/audec-integrated-hardware.json)，
接口与完整播放器的剩余工作见 [MP3.md](MP3.md)。

## 双声道 AUDEC 扩展

独立立体声与 M/S 扩展复用上述工作区和私有 DMA 缓冲，默认链接段大小和原始堆均未增加。
驱动本身仍不分配动态内存；调用方增加右侧 576 个频谱字和 39 个频带字，合计 2460 字节。
三组测试（mono/stereo/joint_stereo）运行前后测试镜像已用堆均为 9376 字节。
这是硬件后端预算，不包括板上 MP3 软件前端或完整播放器。
实测记录见 [audec-stereo-hardware.json](../docs/lib-analysis/generated/recovery/audec-stereo-hardware.json)。

## 接入前内存优化

FM、恢复源码的蓝牙控制器、PCM、串口和 LED 保留在同一固件中。此阶段（提交 `5ffc6ab`）尚未接入 NimBLE，
没有修改控制器恢复算法，也没有重新链接 `libbtctrl.a`。

## 实机内存

单位为字节；`free` 的 total 已扣除 RT-Thread 堆管理结构。
基线为 `51ec6bc`，此前硬件报告见
[bt-rtt-hardware.json](../docs/lib-analysis/generated/recovery/bt-rtt-hardware.json)。

| 状态 | 优化前 | 优化后 |
| --- | ---: | ---: |
| 总堆 | 29160 | 40872 |
| 启动后、未启动蓝牙的可用堆 | 6288 | 28648 |
| 蓝牙运行、未使用 PCM 的可用堆 | 4056 | 26416 |
| 使用过 PCM 后、蓝牙运行的可用堆 | 4056 | 17920 |

常用的蓝牙运行状态多出 **22360 字节（约 21.8 KiB）**，可用空间约为原来的 6.5 倍。
首次 PCM 初始化实际占用 8496 字节，含原有 `2 × 4096` 播放池及框架管理对象。
关闭后保留供复用；FM 直接使用 DAC/DMA，不会触发这笔分配。

## 实现

1. `link_bt_ram.lds` 将 FinSH、PCM 框架和 `pcmcheck` 放入 512 字节对齐的 Flash 段。
   最终 `.flash` 为 14336 字节；计入新增检查和代码布局变化，总堆净增加 11712 字节。
   内核、缓存填充、ROM 适配、射频与音频中断仍在 SRAM，固定蓝牙区和 4 KiB IRQ 栈不变。
   本 BSP 的串口通知及 PCM 完成回调都在线程执行；新增中断回调时必须重新审核其调用链。
2. 新配置 `RT_AUDIO_USING_LAZY_INIT` 让注册 `sound0` 不再立即初始化 PCM。
   `rt_device_open()` 或显式 `rt_device_init()` 时初始化，失败后释放已分配的队列、池和对象，
   可重试。配置默认关闭，只在本综合工程和蓝牙构建回归中启用。
3. UART 原来的 128 项消息队列改为端口待处理位加信号量。同一端口的重复通知合并，
   字节仍从原 RX 环形缓冲读取；清位与 IRQ 原子互斥，处理期间的新 IRQ 留到下一轮。
   动态堆少用 2152 字节，静态通知结构的代价已计入总堆。GPIO 边沿队列保持原容量。

链接时保持当前控制器阶段至少 40 KiB 原始堆区；`memory_report.py` 同时检查关键函数
仍位于正确的 Flash/SRAM 区域。段用量如下，不能把 `.heap` 预留计作已用业务内存：

| 段 | 大小 | 说明 |
| --- | ---: | --- |
| `.comm` | 67704 | 主 SRAM 代码与初始化数据 |
| `.ram1` | 30972 / 31232 | 第二块 SRAM，余 260 字节 |
| `.bss` | 3968 | 普通静态零初始化数据 |
| `.bram` | 6540 / 10240 | 控制器固定 ABI 区 |
| `.heap` | 40960 | 含分配器元数据的堆区 |
| `.stack` | 4096 | 启动/IRQ 栈 |
| `.flash` | 14336 | 普通线程代码与只读数据 |

## 接入前的 NimBLE 预算估算

检查了仓库 `docs/ab32vg1_doc/test_program/ab32v110-ble-test.zip` 的源码和配置，
并用当前 RV32 工具链编译头文件尺寸探针。原包默认单连接；以下只是缓冲区本体：

| 缓冲 | RV32 字节 |
| --- | ---: |
| MSYS：12 × 292 | 3504 |
| HCI 事件：高/低优先级各 8 项，70 字节按 4 对齐 | 1152 |
| ACL：4 项，含 mbuf/header 与对齐 | 1200 |
| HCI 命令 | 260 |
| 合计 | 6116 |

这些数字**不含** Host 线程栈、连接/GATT/安全状态、池管理对象、RTT IPC、服务及应用数据，
也不含 Host 代码。接入时需要将普通 Host 代码放入 Flash，审核 ISR 入口，复用现有控制器
线程。Host 缓冲应放普通 RAM，不能直接照搬旧包全部放 `.btmem` 的做法。

当前可用 26.4 KB 为接入提供了实际空间，但还不是 NimBLE 完整链接/连接测试的结果。
可先按新增数据和堆不超过 16 KiB、正常蓝牙状态余量至少 8 KiB 做接入预算；这是一项
设计目标，不是已测用量。若还需同时保留完整 PCM 播放池，则同一 16 KiB 预算只剩
1536 字节，必须继续调整代码布局或缓冲策略，不能宣称该组合已经够用。
Host 静态池落地后，需要根据静态占用重新设置当前 40 KiB 堆下限，保持总预算检查。

## 验证

- `python3 tools/rtt/test_drivers.py`：六组通过，包括分配失败/重试、录音单方向初始化、
  UART 多端口通知合并及处理期间再入通知。
- `python3 tools/rtt/check_builds.py bluetooth audio`：综合蓝牙与独立音频构建通过。
- `python3 tools/rtt/memory_report.py`：地址、代码归属和堆预算检查通过。
- 烧录与读回校验通过；FM 97.4 MHz 启停、PCM 互斥、DAC 测试音程序、两次静音 PCM
  播放及播放后重新启用 FM 均正常返回。此次未再次请求听感确认，FM 实际广播接收仍待换耳机验证。
- HCI 四项自检通过，电脑 BlueZ 独立发现 `AB32-RTT-TEST`；开发板扫描收到广播报告，
  广播/扫描重复切换后堆不下降。PCM 播放时音频线程栈峰值 30%，shell 峰值 34%；
  BLE 扫描时控制器线程峰值 24%，未缩减线程栈来获取上述收益。

结构化结果与串口记录见
[memory-rtt-hardware.json](../docs/lib-analysis/generated/recovery/memory-rtt-hardware.json) 和
[memory-rtt-hardware.log](../docs/lib-analysis/generated/recovery/memory-rtt-hardware.log)。
以上是接入前的验证范围。NimBLE/GATT 和 ACL 的后续验证见下一节；
FM/蓝牙同时运行和长时间稳定性仍未验证。

## NimBLE 接入后

默认综合配置加入 Apache NimBLE 1.10.0 Host，单连接、MTU 64、无加密测试服务。
控制器固定区与 IRQ 栈没有缩减，Host 栈 3072 字节、控制器栈 2048 字节。
普通 Host 代码放 Flash；NPL、mbuf 队列/池及临界区继续在 SRAM 运行。
这是新功能引入后的完整实测值，不能沿用前面接入前的 40872 字节总堆。

| 段 | 字节 | 说明 |
| --- | ---: | --- |
| `.comm` | 73144 | 主 SRAM 代码与初始化数据 |
| `.ram1` | 31208 / 31232 | 余 24 字节，后续新增内核路径需重新分配段 |
| `.bss` | 9664 | 包含 NimBLE 静态池及状态 |
| `.bram` | 6540 / 10240 | 恢复控制器的固定 ABI 区 |
| `.heap` | 29824 | 原始堆区，扣元数据后 total 为 29736 |
| `.stack` | 4096 | 启动/IRQ 栈 |
| `.flash` | 54784 | shell、PCM 框架、Host/GATT 等线程代码 |

| 运行状态 | 已用堆 | 可用堆 |
| --- | ---: | ---: |
| 上电，蓝牙未启动 | 12224 | 17512 |
| NimBLE 启动，尚未使用 PCM | 18376 | 11360 |
| 已播放 PCM、保留播放池，蓝牙未启动 | 20720 | 9016 |
| NimBLE 启动且保留 PCM 播放池 | 26872 | 2864 |

带 PCM 池和不带 PCM 池的状态各完成三轮真实连接、GATT 读写/通知及重连；
载荷为 1、20、60 字节，MTU 协商到 64。测试前后已用堆一致，HCI errors 和 Host resets
均为零。Host 线程栈峰值 26%，控制器线程 25%。这些是短程功能验证，不代表长期运行证明。
完整 PCM 池保留时可用仅 2864 字节，后续新增协议、安全功能或服务前需要重新预算。

链接脚本和 `memory_report.py` 将带 NimBLE 的原始堆下限设为 28 KiB；
不带 Host 的控制器配置下限为 39 KiB。第二 SRAM 接近满载，原控制器 complex 对象
移到主 SRAM 以容纳 Host 所需的 RT-Thread 内核函数。检查器同时约束 Host Flash 放置，
以及中断、临界区、NPL、mbuf 队列和池函数的 SRAM 地址。

FM 启停与 PCM 互斥回归通过；FM 开启时 `pcmcheck` 返回忙，停止 FM 后播放成功，
蓝牙启用后 FM 启动返回忙。此次没有新增 FM 听感验收，广播接收仍待换耳机验证。
源码、配置、命令与边界见 [NimBLE 接入说明](NIMBLE.md)，最终二进制摘要、连接报告
和串口记录见 [nimble-rtt-hardware.json](../docs/lib-analysis/generated/recovery/nimble-rtt-hardware.json)。

## USB CDC 软件适配后

在 NimBLE 综合工程中加入实验性 USB CDC 后，链接结果如下；本版尚未烧录。

| 段 | 字节 |
| --- | ---: |
| `.comm` | 73200 |
| `.ram1` | 31208 / 31232 |
| `.bss` | 11120 |
| `.bram` | 6540 / 10240 |
| `.heap` | 28320 |
| `.stack` | 4096 |
| `.flash` | 64512 |

USB 代码及 CherryUSB 核心/CDC 放 Flash，由单个线程轮询，USB CPU 中断保持屏蔽。
原始堆比接入 USB 前减少 1504 字节；USB 启动时另申请 2048 字节线程栈及线程对象。
停止后保留工作线程，因此不能把 `usbcdc stop` 当作释放这笔堆内存。
带 USB 的链接和检查器堆下限为 27 KiB，未带 USB 的 28/39 KiB 下限保持不变。

上述是链接预算，不能据此报告运行时 `free`；USB/蓝牙/PCM 各组合须上板重新测量。
尤其先前保留完整 PCM 池的 NimBLE 状态只余 2864 字节，不能假定再启动 USB 仍有足够余量。
原控制器固定内存与栈未缩减，第二 SRAM 仍只余 24 字节。
见 [USB 软件适配](USB.md) 和 [离线结果](../docs/lib-analysis/generated/recovery/usb-rtt-software.json)。

## TF/FAT 软件适配后

在同一综合固件中加入原生 SD0 块设备和 DFS v1/FatFs，默认只注册 `tf0`，
不分配 MMC/SDIO 后台线程。挂载和文件对象按需分配，当前没有 TF 卡，未烧录。

| 段 | 字节 |
| --- | ---: |
| `.comm` | 75200 |
| `.ram1` | 31200 / 31232 |
| `.bss` | 13088 |
| `.bram` | 6540 / 10240 |
| `.heap` | 24352 |
| `.stack` | 4096 |
| `.flash` | 97792 |

相对首次 USB 软件适配，原始堆减少 3968 字节。新 SD0/TF/DFS/FatFs 代码全部在线程执行，
放入 Flash；FinSH 文件处理新引用的 `strstr` 也放 Flash（新增 IRQ 使用者必须重新审核）。
增加文件系统所需内核路径使 SRAM1 首次链接超出 384 字节，因此把内核 `irq.o` 和
`components.o` 移回主 SRAM，所有中断和临界区仍在 SRAM。SRAM1 现只余 32 字节。
DMA staging 固定 512 字节、32 字节对齐，在主 SRAM；脚本验证其位置和大小边界。

带 TF 的原始堆下限设为 23 KiB；其他配置保留 USB 27 KiB、NimBLE 28 KiB、控制器 39 KiB。
这是新增功能后的最低链接预算，并非运行时空闲量。DFS 初始化、挂载、文件对象会占堆，
USB 另需线程栈，PCM 的 8496 字节懒分配仍保留；不能保证这些模块同时启动仍有足够堆。
后续实板应逐项记录 `free`/`ps`，尤其验证 TF + BLE + USB，不沿用旧固件的空闲值。
固定控制器区、IRQ 栈、已有线程栈均未缩减。

默认、TF 综合、USB、NimBLE、旧 SDIO 配置编译通过；TF 适配/FAT 模拟卡、原 SD0 模型、
八组驱动和 USB 离线回归通过。详见 [TF.md](TF.md) 和
[软件验证记录](../docs/lib-analysis/generated/recovery/tf-rtt-software.json)。

## 红外接收软件适配后

同一综合固件加入 ir0 原生 NEC/扩展 NEC 接收器。本版没有遥控器，未烧录。

| 段 | 字节 |
| --- | ---: |
| `.comm` | 76144 |
| `.ram1` | 31200 / 31232 |
| `.bss` | 13312 |
| `.bram` | 6540 / 10240 |
| `.heap` | 23168 |
| `.stack` | 4096 |
| `.flash` | 98816 |

相对 TF 接入版，原始堆减少 1184 字节（代码/静态对象及对齐变化）。IR 不新增线程或动态队列，
事件队列 8 × 12 字节；新增静态设备、互斥锁、统计和引脚恢复状态。
启停不申请堆。普通设备方法和 shell 在 Flash；ISR、IRQ 屏蔽期间的辅助函数、pin 指针表和
编译器生成的内存拷贝/清零、save/restore 调用都在 SRAM，并由检查器核对地址。
蓝牙固定 ABI 区、已有线程栈、IRQ 栈均未缩减，SRAM1 仍余 32 字节。

带 IR 的配置在原有堆下限上减去 1 KiB 作为功能预算，因此当前 TF+IR 下限是 22 KiB；
不带 IR 的各配置下限保持不变。原始堆比此下限多 640 字节，不代表系统运行时只余 640 字节。
实际空闲堆、IRQ 栈峰值、与 BLE/USB/TF/PCM 的组合仍要上板测量，不能沿用旧固件的 free 数值。

默认综合、ir_integrated、独立 ir、tf 和 peripherals 构建通过；IR 模型及编译冲突测试、
八组现有驱动测试、TF/FAT 与 34 场景 SD0 回归通过。验证范围见 [IR.md](IR.md)，
结果记录见 [ir-rtt-software.json](../docs/lib-analysis/generated/recovery/ir-rtt-software.json)。

## 三个板载按键接入后

默认综合配置新增 `BSP_USING_KEYS`，主线程每 10 ms 扫描三个键，复用静态事件队列，
没有额外按键线程或动态分配。GPIO 边沿回调改为可选 `BSP_USING_PIN_IRQ`，默认关闭。

| 段 | 默认综合版字节 | 额外开启 GPIO IRQ 字节 |
| --- | ---: | ---: |
| `.comm` | 76584 | 77320 |
| `.ram1` | 31200 / 31232 | 31200 / 31232 |
| `.bss` | 13504 | 13920 |
| `.bram` | 6540 / 10240 | 6540 / 10240 |
| `.heap` | 22528 | 21376 |
| `.stack` | 4096 | 4096 |
| `.flash` | 100864 | 101376 |

相对 IR 接入时，默认原始堆区减少 640 字节，仍满足 TF+IR 的 22 KiB 下限。
主线程扫描和 shell 代码放 Flash，GPIO 模式配置及中断屏蔽路径保留 SRAM。
默认不再在启动时分配旧 GPIO 消息队列/线程，所以不能仅由原始堆区变化推算运行时空闲量。

可选 GPIO IRQ 使用逐引脚静态回调表、信号量和互斥锁；链接预算额外预留 2 KiB，
因此该配置堆区下限为 20 KiB。首次启用 GPIO IRQ 时另从堆分配 1024 字节工作线程栈及对象，
后续禁用保留线程。这不影响默认按键扫描。未减少蓝牙 ABI 区或已有业务线程栈，SRAM1 仍余 32 字节。
链接区大小不等于运行时可用堆，组合开启 BLE、USB、TF、PCM 仍需逐项测量。

默认综合、keys_irq、ir_integrated、peripherals、i2s_pa 构建及放置检查通过；
按键/GPIO 模型、IR、八组驱动和 TF/FAT/SD0 回归通过。见 [KEYS.md](KEYS.md) 及
[软件验证记录](../docs/lib-analysis/generated/recovery/keys-rtt-software.json)。

该综合固件随后已烧录并完成三键/长按验证。启动和按键测试后的 `free` 均为 total 22440、
used/maximum 9376、available 13064 字节，测试没有增加堆占用。该状态下 BLE、FM、USB、IR
均未启动，TF 未挂载，不能作为全模块运行时的余量。见
[实机记录](../docs/lib-analysis/generated/recovery/keys-rtt-hardware.json)。

## RTC 修复与接入后

默认综合配置新增原生 RTC 秒计数设备，首次访问才初始化。无新线程或动态缓冲，
静态设备和互斥锁串行化操作。已烧录验证，以下为包含 `rtcctl status` 的最终版链接结果。

| 段 | 字节 |
| --- | ---: |
| `.comm` | 76048 |
| `.ram1` | 31200 / 31232 |
| `.bss` | 13728 |
| `.bram` | 6540 / 10240 |
| `.heap` | 22848 |
| `.stack` | 4096 |
| `.flash` | 105984 |

RTC 事务、忙轮询和桥配置使用 `.irq.rtc` 留在 SRAM，普通设备入口、RTC 框架和
libc 的日历/时间访问放 Flash。时间访问使用互斥锁，仅允许线程上下文，不能从 ISR 调用。
迁移日历代码后，原始堆比按键接入版增加 320 字节；仍保持 22 KiB 下限，没有压缩已有线程栈、
IRQ 栈或蓝牙固定区。SRAM1 仍余 32 字节。

`memory_report.py` 增加 RTC 临界区、编译器 save/restore 及 Flash 回调位置检查；
默认综合、rtc_integrated、rtc、rtc_external、peripherals 和不带 RTC 的 keys 构建通过。
RTC 内外时钟模型、既有驱动和 TF/FAT/SD0 回归通过。最终时钟修正后重新通过默认综合、
rtc_integrated、rtc_external 构建和内外时钟模型。实板约 60 秒走时、范围与 libc 访问测试前后均为：

```text
total    : 22760
used     : 9376
maximum  : 9376
available: 13384
```

该数字为启动后和 RTC 测试状态，不代表蓝牙、音频、USB 同时运行的余量。
长期精度及断电保持尚未验证。见 [RTC.md](RTC.md) 和
[实板记录](../docs/lib-analysis/generated/recovery/rtc-rtt-hardware.json)。

## S2 呼吸灯接入后

默认增加 Timer5 PWM0 和红灯亮度控制，复用 main 的 10 ms 按键扫描周期。
没有新增线程、栈或动态缓冲；PWM 静态设备、状态及互斥锁占用少量 RAM。
PWM 框架、驱动、呼吸灯和 main 线程正文放 Flash，GPIO 临界代码仍在 SRAM。

| 段 | 字节 |
| --- | ---: |
| `.comm` | 76192 |
| `.ram1` | 31200 / 31232 |
| `.bss` | 13824 |
| `.bram` | 6540 / 10240 |
| `.heap` | 22624 |
| `.stack` | 4096 |
| `.flash` | 111104 |

原始堆比 RTC 版减少 224 字节，仍通过 22 KiB 下限，未修改既有栈和蓝牙固定区。
实板呼吸运行时 `free` 为 total=22536、used=9376、maximum=9376、available=13160。
此数据不代表蓝牙、音频和 USB 同时运行时余量。
功能与验证见 [PWM.md](PWM.md) 和 [实机记录](../docs/lib-analysis/generated/recovery/pwm-led-rtt-hardware.json)。

## 红外遥控器实测及长按修复后（2026-11-03）

新增最近完整帧 tick 和待释放状态，由现有 main 每 10 ms 执行短 SRAM 临界检查。
没有新线程或动态缓冲；仅初始化时调用的 `hal_pwm_mspinit` 移至 Flash，以保持原有预算。

| 段 | 字节 |
| --- | ---: |
| `.comm` | 76280 |
| `.ram1` | 31200 / 31232 |
| `.bss` | 13824 |
| `.bram` | 6540 / 10240 |
| `.heap` | 22528 |
| `.stack` | 4096 |
| `.flash` | 111104 |

相对 S2 呼吸灯版原始堆减少 96 字节，恰好满足 22 KiB 下限，未降低预算。
IR 测试前后及启停后 `free` 均为 total=22440、used=9376、maximum=9376、available=13064。
这些数值不代表所有外设同时运行的余量。见 [IR.md](IR.md) 和
[本次实板报告](../docs/lib-analysis/generated/recovery/ir-rtt-hardware.json)。

## 电源管理与浅休眠/软关机接入后（软件与实板验证）

新增静态 power0 设备，复用 RTC 互斥锁和 SRAM 传输桥，无新线程或动态缓冲。
PM 线程回调及命令放 Flash，睡眠入口/桥策略/终端断电路径留 SRAM；
仅线程使用的 FM 命令、蓝牙 btctl/diagnose 也迁至 Flash，
底层 FM 射频临界函数仍留 SRAM。

| 段 | 字节 |
| --- | ---: |
| `.comm` | 75376 |
| `.ram1` | 31224 / 31232 |
| `.bss` | 13888 |
| `.bram` | 6540 / 10240 |
| `.heap` | 23360 |
| `.stack` | 4096 |
| `.flash` | 116736 |

相比红外长按修复版，原始堆净增加 832 字节，原有 22 KiB 下限不变。
本版已烧录，连续 10 次 RTC 唤醒前后 total=23272、used=9376、available=13896 字节不变。
RTC 计数读取/睡眠/补 tick 的临界路径及调用目标留 SRAM；SRAM1 剩余 8 字节。
见 [POWER.md](POWER.md) 和 [软件记录](../docs/lib-analysis/generated/recovery/power-rtt-software.json)。
