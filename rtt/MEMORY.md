# 综合 RTT 内存优化（2026-11-02）

FM、恢复源码的蓝牙控制器、PCM、串口和 LED 保留在同一固件中。此次没有接入 NimBLE，
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

## NimBLE 后续预算

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
NimBLE/GATT、连接与 ACL 数据传输、FM/蓝牙同时运行和长时间稳定性尚未验证。
