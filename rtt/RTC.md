# AB32VG1 原生 RTC

AB32VG1 支持 RTC。[寄存器手册第 5 章](../docs/ab32vg1_doc/AB32VG1_Register.pdf)
列出独立供电域的 32 位秒计数器、闹钟、秒中断及唤醒相关寄存器；
[数据手册](../docs/ab32vg1_doc/AB32VG1_DataSheet.pdf) 也列有 IRTC 和 OSC32KI/OSC32KO 引脚。
这些是芯片能力，不能等同于当前驱动全部实现。

综合 RTT 默认启用 `RT_USING_RTC`、`BSP_USING_ONCHIP_RTC` 和 `RTC_USING_INTERNAL_CLK`，
注册名为 `rtc` 的设备，提供秒级 UTC Unix 时间读写。修复版已烧录并通过 Flash CRC 校验。
实板调试发现并修正了时钟路径选择问题，测试详情见下文；长期精度和掉电保持仍未验证。

## 时钟与供电

- 默认沿用原 AB32 BSP 的内部 32K 路径：启用 RCEN、**置位 RTCCON0 bit6**、关闭 XOSC32K，
  并选择 32K 分频档。V2.0 实测 CON0=0x21 时能读写但不走时，仅置 bit6 后 CON0=0x61，
  计数恢复递增；CON2 均为 0xca。
- 手册将 bit6 称为 EXT32KS，原 BSP 命名为 INTERNAL_32K。此前仅按手册名称推断默认应清零，
  实测证明该判断不适用于此板的内部时钟路径。代码现用中性名称 `RTC_CON0_32K_SOURCE_SELECT`，
  以原 BSP 配置和实板对比结果为依据，不把位名直接解释为板外晶体选择。
- 关闭 `RTC_USING_INTERNAL_CLK` 的可选构建沿用原 BSP，清 bit6、启用 XOSC32K 和 32.768K 档，
  只适用于已确认合适外部时钟硬件的情况。
- [V02 原理图](../docs/ab32vg1_doc/AB32VG1_Prougen_Schematic_V02.pdf) 中 OSC32K 相关器件标为 NC，
  没有已装的 32.768 kHz 晶体；板上的 Y1 是 26 MHz，不能当成 RTC 晶体。
- 内部 RC 尚未校准，不承诺长期走时精度。独立供电域意味着有条件保留计数，
  不代表开发板拔掉所有电源后还能计时；V2.0 的供电、RESET 行为和备电方案需实测核对。

## 首次校时与读取

上电仅注册设备；首次 open/control 时初始化时钟和传输桥，不创建线程、不分配动态缓冲。
初始化保留原计数、首次上电标志、RTC RAM 和无关电源配置，删除了硬编码的 2021 年日期。
首次上电标志为 1 时，时间视为未设置，读取返回 `-RT_EIO`，不会报告伪造的有效日期。

烧录本版后，在 CH340/UART0 shell 中执行：

```text
list device
rtcctl get
rtcctl status
```

首次读取若提示时间未设置，先在已校准时间的 Linux 电脑执行 `date +%s` 获取当前 UTC Unix 秒数，
把结果替换到下面的 `<秒数>`（不输入尖括号），尽快发给开发板：

```text
rtcctl set <秒数>
rtcctl get
date
```

`rtcctl` 使用 UTC 秒数；RT-Thread 的 `date` 按本工程时区 UTC+8 显示本地日期。
有效校时后也可使用 `date 年 月 日 时 分 秒` 修改本地日期时间。
上游 `date` 的设置流程先读取旧时间，因此未校时状态须先用 `rtcctl set` 或 `set_timestamp()`，
不能仅执行 `date ...` 完成首次校时。命令报错后应先解决错误再读取，不能只看 shell 提示符判断成功。

硬件计数范围为 0..4294967295 秒，未实现到顶回绕后的纪元扩展。
**当前 RV32 工具链的 `time_t` 是有符号 32 位**，因此实际时间 API 范围为
0..2147483647 秒，即 UTC 1970-01-01 至 2038-01-19 03:14:07；实板确认更大的值被拒绝，
不改变原时间。`rtcctl status` 显示原始寄存器、计数、valid 标志和 time_t 位宽；
valid 表示时间已经设置，不单独证明振荡器正在走时。支持更晚日期需要另外迁移时间 ABI。

应用在线程上下文使用 `get_timestamp()` / `set_timestamp()`，或
`rt_device_control(dev, RT_DEVICE_CTRL_RTC_GET_TIME/SET_TIME, &seconds)`。
参数为 `time_t *`；错误时读取输出保持不变。允许直接 control，无需依赖先 open 才完成硬件初始化。
这些接口使用互斥锁，不能从 ISR、启动调度前或其他不能阻塞的上下文调用。

## 驱动修复

[drv_rtc.c](bsp/libraries/hal_drivers/drv_rtc.c) 现在采用明确返回错误的原生寄存器传输，
移除漏返回值的旧 `irtc_sfr_read()` 和无错误返回的内部封装：

- 使用实际芯片头文件的命令编码：读 `0x100`、写 `0x200`，计数器命令 `0x01`。
  多字节计数按 MSB 优先显式组装，不依赖主机端序。
- 每次传输最多轮询 1024 次，RTCPND bit16 为 1 表示忙。轮询不依赖被屏蔽的系统 tick；
  超时返回 `-RT_ETIMEOUT`，退出时释放 CS、恢复原中断状态及互斥锁，允许重试。
  1024 是轮询次数，不是已标定的毫秒时限。
- 互斥锁覆盖完整初始化/读写操作；只有短寄存器事务屏蔽中断。事务、等待及相关调用均放 SRAM，
  普通 RTC 回调、框架和日历转换在线程中执行，放 Flash。
- 手册未保证四字节读数锁存，驱动比较连续采样，只接受相同或相差一秒的计数，有限重试跨秒撕裂值；
  持续不一致返回错误，不把不完整数值交给应用。
- 设置时间前将可读写的首次上电标志置为“未校时”，全部写入成功后才清除。
  任一步传输失败都会保留不确定状态，须重新成功校时，避免部分写入被当成有效时间。
- 初始化关闭本驱动没有处理的秒/闹钟中断，保留无关唤醒/复位配置；未实现闹钟 API、
  秒事件、低功耗唤醒、RTC RAM 用户接口、亚秒精度或自动时间同步。

## 验证

```sh
python3 tools/rtt/test_rtc.py
python3 tools/rtt/test_drivers.py
python3 tools/rtt/check_builds.py default rtc_integrated rtc rtc_external peripherals keys
python3 tools/rtt/memory_report.py
```

RTC 专用模型使用真实芯片头文件和真实驱动，模拟命令/数据桥，启用 UBSan 和返回值检查。
覆盖内部/外部时钟、保留位、首次上电、重复初始化、UTC 秒范围、字节序、跨秒读数、
每个初始化/读写字节的超时、忙状态、部分写入、失败重试、中断/互斥恢复及命令参数。
替换了旧测试中仅检查 RTCDAT 最后一次写入的做法；旧模型里的错误命令常量已删除。
八组既有驱动和 TF/FAT/SD0 回归通过，内外时钟、综合 RTC 及不带 RTC 配置构建通过。
这些测试不模拟真实振荡器、时钟精度或供电保持。

当前综合固件原始堆区 **22848 字节**，满足原有 22 KiB 下限，没有下调预算；
SRAM1 仍余 32 字节，蓝牙固定区和 IRQ 栈不变。见 [MEMORY.md](MEMORY.md) 和
[首轮软件验证记录](../docs/lib-analysis/generated/recovery/rtc-rtt-software.json)。该记录对应烧录前的
历史版本，时钟选择假设随后被实板推翻，最终版本以以下实测记录和固件哈希为准。

### V2.0 实板验证（2026-11-02）

最终综合固件通过 CH340 `/dev/ttyUSB0` 下载，Flash 写入及 CRC 校验通过，无需手动 RESET。
首次版本能读写但不走时；诊断版仅置位 CON0 bit6 后恢复递增，最终版固化此配置，
移除了临时时钟切换命令，保留只读 `rtcctl status`。

```sh
python3 tools/rtt/test_rtc_uart.py --port /dev/ttyUSB0 --seconds 60 \
  --output docs/lib-analysis/generated/recovery/rtc-rtt-uart.json
```

运行前退出 minicom 释放串口。脚本会短暂改写 RTC 为测试值，结束时恢复电脑当前 UTC 时间。

- 零值、字节序测试值 `0x12345678` 和接近有符号 32 位上限的值写入/读回通过。
- `2147483648`、负数、超出硬件 32 位范围以及非数字参数被拒绝，原时间继续保持。
- 12 个样本覆盖电脑单调时钟 60.512 秒，RTC 增加 61 秒；每次采样均在两秒容差内。
  这是秒级走时检查，包含采样相位与串口延迟，不能据此计算长期频率误差。
- 期间反复调用 `date` 未重置计数；测试前后可用堆均为 13384 字节，按键读取错误为零。
- 测试结束重新校时，`date` 显示 `2026-11-02 17:33:30 UTC+8`，串口已释放。

记录见 [最终固件及结果摘要](../docs/lib-analysis/generated/recovery/rtc-rtt-hardware.json) 和
[完整串口记录](../docs/lib-analysis/generated/recovery/rtc-rtt-uart.json)。
尚未验证手动 RESET、彻底断电后的保持、长期温漂、外部晶体，以及同时运行蓝牙/音频/USB 的组合。
