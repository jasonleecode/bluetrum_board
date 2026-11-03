# 板载红外接收软件适配

板上标为 **IRDA** 的接口接收遥控器信号。本版使用 AB32 原生 IR 接收控制器，
提供 NEC / 扩展 NEC 的 32 位按键接收，**不实现 IrDA 数据链路、红外发送、RC5/RC6 或空调长码**。
驱动已默认编入包含 FM、蓝牙/NimBLE、USB、TF 的同一综合 RTT 工程。
已使用一只扫地机器人遥控器收到有效 NEC 帧（地址 `0x4c`），物理接收链路与协议兼容。
实测还发现该遥控器完整帧的重复间隔长于旧驱动的释放超时，已修正应用层释放判定。
实测记录与剩余边界见下文。

默认 [收音机应用](RADIO.md) 在 S2 开启 FM 时启动并读取红外队列，用上下键调音量、
左右键调频，S2 关闭时停止接收。独立使用下文的 `irrx read/watch` 前先执行 `radio stop`。

## 接线与依据

- V02 板：短接 **P1 的 1–2**，将接收器输出接至 **PE6**。
  同一排 P1 的 3–4 是红灯跳线，请按编号区分。
- 旧版手册 §2.10：对应跳线 **J17**。
- 电路图接收头为 3.3 V 供电、DAT 输出。这里使用板载接收头，不直接连接裸红外二极管。

依据为仓库内的 [V02 原理图](../ab32vg1_doc/AB32VG1_Prougen_Schematic_V02.pdf)、
[用户手册](../ab32vg1_doc/AB32VG1_User_Manual.pdf)、
[数据手册 PE6 引脚复用表](../ab32vg1_doc/AB32VG1_DataSheet.pdf)、
[寄存器手册第 3 章](../ab32vg1_doc/AB32VG1_Register.pdf) 和原开源 `drv_irrx.c`。
PE6 是 **IR_G7**；旧代码的 G6 注释与数据手册不一致。
寄存器手册的 FUNCMCON2 表省略了 IR 映射字段说明，本版沿用开源 BSP 的 [23:20] 字段，
采用两次独立写入“清除 0xf、选择 7”，避免读改写误触发其他映射字段。
PE6/G7 映射及当前时钟/容差已能接收本次遥控器；其他载波频率及协议未验证。

这是对已有开源 BSP 接收驱动的完善，没有新增闭源库依赖，也不宣称完成了某个 `.a` 对象的差分恢复。

## 使用

编译和下载流程见 [BSP 说明](../../rtt/README.md#linux-构建与驱动维护)。有遥控器并下载此版本后，
使用原 CH340 串口的 115200、8N1 shell：

```text
irrx start
irrx status
irrx watch 10
irrx read
irrx status
irrx stop
```

`start` 开启接收，`stop` 关闭 IR 和对应 IRQ，恢复 PE6 原引脚状态、IR 映射及原时钟门控。
上电只注册 `ir0` 和静态互斥锁，不配置红外硬件，不新增线程或动态队列。
`read` 读取当前队列，`watch [秒]` 默认观察 10 秒，允许 1–30 秒，到时返回 shell；期间让出线程。
watch 不隐式启动接收，也不会在退出时停止硬件。read/watch 会消费应用同一条事件流。

示例输出格式（非实测结果）：

```text
IR DOWN NEC addr=0000 cmd=12 raw=ed12ff00 tick=1234
IR UP NEC addr=0000 cmd=12 raw=ed12ff00 tick=1344
```

- `DOWN`：收到合法新按键。切换按键时先报告旧键 `UP`。
- `REPEAT`：按住期间再次收到相同的**完整 32 位帧**。
  NEC 短重复码由硬件维护按住/超时状态，手册未给出独立重复中断位，本驱动不会为短重复码伪造事件。
- `UP`：收到硬件释放标志，且最近一个合法完整帧已过去至少 400 ms；
  由现有 main 每 10 ms 检查。换键、无效帧或显式 stop 会立即释放旧键。
  RX 和释放标志同时积累时先报告帧，再等待同样的释放窗口。
  约 400 ms 的延迟用于兼容较慢的完整帧重复；连续同键短按若间隔不足此窗口，
  接收端无法与长按重复可靠区分，会合并为 REPEAT，应用应按需求决定动作。
- 命令字节与反码必须互补；地址也互补时按标准 NEC 显示 8 位地址，否则按扩展 NEC 显示 16 位地址。
  这两种编码在某些地址上本来就无法仅凭帧内容区分；不根据按键值推断遥控器品牌。
- status 的 `raw` 是最后合法按键，`pressed` 表示是否仍有效；`frames/invalid/releases` 为累计计数。
  `releases` 统计硬件释放标志，不等于应用层 UP 事件个数。
  `queued` 最多 8 个事件，溢出丢弃最旧事件并增加 `dropped`。发生丢失后应用应通过 status 重新同步键状态。

## 应用接口

包含 `drv_irrx.h`，使用 RTT 标准设备 API，调用方在线程中执行：

```c
rt_device_t dev = rt_device_find("ir0");
struct ab32_ir_event events[4];
struct ab32_ir_status status;
if (dev && rt_device_open(dev, RT_DEVICE_OFLAG_RDONLY) == RT_EOK)
{
    rt_device_control(dev, AB32_IR_START, RT_NULL);
    /* Nonblocking, returns bytes; process n / sizeof(events[0]) records. */
    rt_ssize_t n = rt_device_read(dev, 0, events, sizeof(events));
    if (n > 0) { /* process events */ }
    rt_device_control(dev, AB32_IR_GET_STATUS, &status);
    rt_device_control(dev, AB32_IR_STOP, RT_NULL);
    rt_device_close(dev);
}
```

事件为固定 12 字节：原始 32 位帧、32 位 RTT tick、事件类型及保留字节。
read 的请求/结果单位为**字节**，请求必须是完整事件的倍数，每次最多返回 8 个。
调用者缓冲可未对齐；无数据返回 0，参数错误返回负值。设备 close 不自动停止全局接收器。
启动会清除上次停止后的队列与统计；已经运行时重复 start 不清除。停止保留队列，并为当前按键补 UP。
不在 ISR 内调用用户回调，也没有 rx_indicate 通知，请由应用线程轮询。
原资料中的 `ab32_get_irkey` 无限打印示例已由此事件接口和有时限的 watch 命令替代。

## 资源与限制

IR 使用独立硬件解码器的 1 MHz 时钟模式、32 位数据模式，沿用原 BSP 容差：
引导/重复容差 1001 μs、逻辑 1 容差 251 μs、逻辑 0 容差 126 μs、释放超时约 110 ms。
显式打开 CLKGAT1 的 IR 时钟门控；不修改 CPU/蓝牙/USB 的 PLL。
本次遥控器的接收已验证；未测量载波频率或时钟精度。
遥控器长按时完整帧约每 157 ms 一帧，起始还观察到约 314 ms 间隔，
因此原 110 ms 硬件释放会过早到达。现在保留该硬件配置，通过 400 ms 软件宽限衔接完整帧，
不依赖新增 IRQ 或线程。标准 NEC 短重复码不产生完整帧，因此只凭 last_frame 超时不能释放；
仍须同时等到硬件释放标志。

IR 与 TIMER3/PWM 共用 **IRQ 6**，与 **SPI1 G4 的 PE6 时钟**冲突；Kconfig 与编译检查约束这些组合。
应用不能同时用 GPIO/ADC 或其他复用功能驱动 PE6。旧软件捕获解码分支已移除，不能同时启用。
TF 使用 PB0–PB2、USB 使用 PB3/PB4，不直接占用 PE6；与其他模块的中断延迟和共存仍待实测。

队列、设备和状态固定分配。IRQ 及屏蔽中断期间的所有操作在 SRAM 执行，shell/设备入口在 Flash；
没有缩减任何已有线程栈、控制器固定区或 IRQ 栈。
首次 IR 接入版原始堆区为 **23168 字节**，比接入 IR 前少 1184 字节；这不是运行时空闲堆。
后续按键接入版的当前预算见 [MEMORY.md](MEMORY.md)。
SRAM1 仍使用 31200 / 31232 字节，最新预算见 [MEMORY.md](MEMORY.md)。

## 软件验证与后续实测

```sh
python3 tools/rtt/test_ir.py
python3 tools/rtt/test_drivers.py
python3 tools/rtt/test_tf.py
python3 tools/rtt/check_builds.py ir_integrated ir tf peripherals
python3 tools/rtt/memory_report.py
```

离线测试执行真实驱动，用模型提供 MMIO/W1C/映射寄存器和 IRQ，启用 UBSan。
已覆盖无硬件访问的启动注册、重复启停、时钟/引脚/其他映射保持、NEC 与扩展地址、命令 0、
重复帧、换键、松键、错误反码、同时 pending、清标志期间新数据、队列溢出、未对齐读取、
tick 回绕、有时限 watch，以及冲突配置的编译拒绝。新增实测 314/157 ms 间隔回放、
400 ms 释放边界、没有硬件释放时保持短重复码长按状态、延迟释放跨 tick 回绕测试。
检查器还验证 ISR、释放轮询、临界区、pin 表、
编译器生成的 memcpy/memset/save/restore 等调用目标在 SRAM。
结构化记录见 [ir-rtt-software.json](../lib-analysis/generated/recovery/ir-rtt-software.json)。

## V2.0 实测（2026-11-03）

本次为用户提供的扫地机器人遥控器，品牌型号未知。无需配对，板载接收头和当前 NEC 解码器兼容。
用户按指定顺序对“上、右、左、下”各长按后松开，得到以下对应关系：

| 按键 | NEC 地址 | 命令 | 原始帧 |
| --- | --- | --- | --- |
| 上 | `0x4c` | `0x4a` | `0xb54ab34c` |
| 右 | `0x4c` | `0x41` | `0xbe41b34c` |
| 左 | `0x4c` | `0x19` | `0xe619b34c` |
| 下 | `0x4c` | `0x52` | `0xad52b34c` |

修复前在同一键长按中反复出现 DOWN/UP：硬件约 102–110 ms 即报告释放，而完整帧间隔
通常为 156–157 ms、起始有约 313–314 ms 间隔。暂停读取期间还出现队列覆盖，
这是 8 项队列溢出，不能当作空中传输错误；当时 invalid 始终为零。

修复后的综合固件已重新编译、烧录并通过 Flash CRC 校验。30 秒连续观察共收到 **53 帧**，
事件为 **4 DOWN、49 REPEAT、4 UP**；最后一帧到 UP 的间隔为 403/406/404/404 ms。
结束时 pressed=0、queued=0、dropped=0、invalid=0。
stop 后 running=0，重新 start 后 running=1 且统计清零；最后保留接收开启，串口已释放。
此次没有在 stop 期间额外照射遥控器，因此不将“停止期间无接收 IRQ”标为硬件验证通过。

测试前后可用堆均为 **13064 字节**，无新增线程或动态缓冲；原始堆 22528 字节，
仍通过 22 KiB 下限。为保持预算，把仅设备初始化线程调用的 PWM 引脚配置函数放到 Flash，
新增 IR 释放轮询仍在 SRAM，未缩减已有栈或蓝牙保留区。

完整串口日志、修复前后对照、固件哈希见
[实板记录](../lib-analysis/generated/recovery/ir-rtt-hardware.json)。
当前只验证这一只遥控器的标准 NEC 完整帧重复；扩展 NEC、短重复码保持、错误帧恢复等仍为模型覆盖，
未用真实信号发生器验证。没有测量载波、接收距离或角度，也未验证 BLE/USB/TF/FM 同时运行。
