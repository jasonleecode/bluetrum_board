# 板载红外接收软件适配

板上标为 **IRDA** 的接口接收遥控器信号。本版使用 AB32 原生 IR 接收控制器，
提供 NEC / 扩展 NEC 的 32 位按键接收，**不实现 IrDA 数据链路、红外发送、RC5/RC6 或空调长码**。
驱动已默认编入包含 FM、蓝牙/NimBLE、USB、TF 的同一综合 RTT 工程。
目前没有遥控器，已完成软件适配、离线测试、编译和 DCF 打包。
后续按键接入版综合固件已烧录，确认 ir0 注册，**尚未做实际红外接收测试**。

## 接线与依据

- V02 板：短接 **P1 的 1–2**，将接收器输出接至 **PE6**。
  同一排 P1 的 3–4 是红灯跳线，请按编号区分。
- 旧版手册 §2.10：对应跳线 **J17**。
- 电路图接收头为 3.3 V 供电、DAT 输出。这里使用板载接收头，不直接连接裸红外二极管。

依据为仓库内的 [V02 原理图](../docs/ab32vg1_doc/AB32VG1_Prougen_Schematic_V02.pdf)、
[用户手册](../docs/ab32vg1_doc/AB32VG1_User_Manual.pdf)、
[数据手册 PE6 引脚复用表](../docs/ab32vg1_doc/AB32VG1_DataSheet.pdf)、
[寄存器手册第 3 章](../docs/ab32vg1_doc/AB32VG1_Register.pdf) 和原开源 `drv_irrx.c`。
PE6 是 **IR_G7**；旧代码的 G6 注释与数据手册不一致。
寄存器手册的 FUNCMCON2 表省略了 IR 映射字段说明，本版沿用开源 BSP 的 [23:20] 字段，
采用两次独立写入“清除 0xf、选择 7”，避免读改写误触发其他映射字段。
这些映射与时序仍需要实板验证，不能把模型测试视为对硬件行为的证明。

这是对已有开源 BSP 接收驱动的完善，没有新增闭源库依赖，也不宣称完成了某个 `.a` 对象的差分恢复。

## 使用

编译和下载流程见 [BSP 说明](README.md#linux-构建与驱动维护)。有遥控器并下载此版本后，
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
- `UP`：硬件释放标志、无效帧使当前键失效，或显式停止接收。
  RX 和释放标志同时积累时按“帧、释放”顺序处理，最终不保持按下状态。
- 命令字节与反码必须互补；地址也互补时按标准 NEC 显示 8 位地址，否则按扩展 NEC 显示 16 位地址。
  这两种编码在某些地址上本来就无法仅凭帧内容区分；不根据按键值推断遥控器品牌。
- status 的 `raw` 是最后合法按键，`pressed` 表示是否仍有效；`frames/invalid/releases` 为累计计数。
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
真实时钟、接收头载波匹配、长按释放和解码方向都必须通过遥控器验证。

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
tick 回绕、有时限 watch，以及冲突配置的编译拒绝。检查器还验证 ISR、临界区、pin 表、
编译器生成的 memcpy/memset/save/restore 等调用目标在 SRAM。
结构化记录见 [ir-rtt-software.json](../docs/lib-analysis/generated/recovery/ir-rtt-software.json)。

待有遥控器后：确认跳线和接收器输出，验证短按/长按/连续换键、标准/扩展 NEC、
错误信号下的恢复、停止后无 IRQ、重新启动，以及与 BLE/USB/TF/FM 的资源共存。
