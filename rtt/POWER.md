# 电源管理驱动

`drv_power.c` 默认编入综合 RTT，注册 **power0** 和 **pmctl**。已实现状态读取、PWKEY 配置、
保留时钟的浅休眠入口，以及通过 RTC 电源域软关机、唤醒后重新启动的流程。
**已烧录 V2.0 实测：连续 10 次 RTC 唤醒休眠均正常返回，计时及堆检查通过。**
软关机/PWKEY 唤醒未测试（尚未接外部按键），电流和深度休眠也未测量。

## 模式和恢复方式

| 操作 | 实现 | 恢复方式 |
| --- | --- | --- |
| `pmctl sleep` | 对齐 RTC 秒边界，暂停 Timer0，保留 PLL/电源轨/RAM，写 LPMCON bit 0 | RTC 下一秒唤醒；补偿系统 tick，恢复 Timer0/中断策略并返回命令行 |
| `pmctl off [pwkey\|vusb\|both]` | 配置唤醒源，设置 LDO、关闭 LVD，清除 CORE/IO/BUCK 使能并提交 LPMCON 请求 | 唤醒触发复位，经 ROM 和正常 RTT 初始化重启；不恢复原线程、连接或未保存状态 |

首次实测证明 **Timer0 不能唤醒此处的 LPMCON 睡眠**：旧版进入后串口不响应，需要 RESET。
修复后使用 RTCCON bit 7 的独立 RTC 秒唤醒，不使用标准 WFI。
先在可调度的线程中等待一个 RTC 秒边界（最多 2 秒，RTC 不走则报错而不入睡），随后锁定
调度器、重新检查外设状态，关中断并暂停 Timer0，清除旧 RTC 秒标志并启用秒唤醒。
返回后清除睡眠请求，读取 RTC 的秒增量并补偿系统 tick，再恢复 Timer0 和原中断/唤醒策略。
到期的 RTT 定时器交给下一个正常 tick ISR 处理，不在电源临界区执行回调。

实际睡眠约 1 秒；由于进入前要对齐秒边界，命令总耗时通常 1–2 秒。
对齐避免了直接用整数秒补偿导致每次多算近一秒的问题；仍有边界采样及 RTC 振荡器精度误差。
进入过程中若被其他硬件源提前唤醒，会再次睡眠直到 RTC 秒标志到达；命令输出 entries 便于检查。
全局中断在实际睡眠期间关闭，因此不能承诺这段时间内串口、IR 或按键事件不丢失。
这不是蓝牙连接保持模式，活动外设仍会阻止进入。

没有关闭 PLL/XO，**不等同于手册的最低功耗 Sleep 条件**。没有接入 RT_USING_PM 自动调度、
idle hook、自动 tickless、PLL 关闭后的深睡恢复、RTC 闹钟唤醒或 PWKEY 按键事件流。
RTC 日期时间读写见 [RTC.md](RTC.md)。

## 硬件与实现依据

- [AB32VG1 寄存器手册第 5 章](../docs/ab32vg1_doc/AB32VG1_Register.pdf) 定义
  RTC 独立电源域：CON3 高位为唤醒使能，低 3 位为 CORE/IO/BUCK；RTCCON bit 5
  为 power-down 唤醒复位使能。
- [V02 原理图](../docs/ab32vg1_doc/AB32VG1_Prougen_Schematic_V02.pdf)：PWKEY 是芯片第 18 脚
  PWRKEY，引出到 **ANALOG IN 排针第 4 脚**，没有连接 S2/S3/S4。
- 同系列 SDK 的 [func_lowpwr.c](https://github.com/ZhiqingLi/Sdk_Refresh/blob/aa0fc8f993bd0c2a6982bf0a95b3461ad7e7aa6d/AB53XX_SDK/app/platform/functions/func_lowpwr.c)
  中 func_sleepmode 与 sfunc_pwrdown 提供 LPMCON bit 0、WK 重新使能、断电后复位的时序参考。
  同份 libplatform.a:power.o 的 charge_lowpower_power_down 也包含关电源轨后写 LPMCON 的序列。
  寄存器地址与 AB32 头文件一致，但这是**同系列实现证据，不代替 AB32VG1 实测**。
- [NuttX 的 AB32VG1 idle 实现](https://github.com/apache/nuttx/blob/master/arch/risc-v/src/ab32vg1/ab32vg1_idle.c)
  明确说明此内核不能使用标准 wfi；本驱动使用专有寄存器入口。

本版软关机没有复制 SDK 中降低 VDDIO、开启 FM 加速掉电等产品专用操作，也没有关闭 PLL/XO。
提交关机后电源轨的实际状态、开发板是否存在外部供电回灌、RTC 保持和静态电流均待测量。
芯片软关机不代表 CH340 或开发板其他外部供电器件也断电。

## 命令

```text
pmctl status
pmctl input on on       # WK 数字输入、上拉；同时关闭下拉
pmctl filter 32         # off / 8 / 32 / 128 / 512，单位 ms
pmctl wake pwkey on     # 或 off
pmctl wake vusb on      # 或 off
pmctl sleep
```

input 两个参数分别为输入使能、上拉使能。保留上拉强度及模拟配置；若 WK 正被配置为模拟功能，
OFF 会拒绝操作。wake 仅更改指定使能位，不进入睡眠、不改电源轨。
启动时不自动执行这些配置，也不会自动休眠或关机。

status 输出 WK 原始电平、芯片 VUSB 在线状态、输入/上拉/滤波、唤醒使能、当前锁存标志、
电源轨和原始寄存器；boot_* 是组件初始化阶段记录的原始标志。
boot_valid=0 表示未成功采样。锁存标志可能来自更早的事件或受 ROM 启动影响，
不能直接当作可靠的本次启动原因；状态采样不清 pending、不校准时间。

### 进入条件

两种模式均拒绝 ISR、无当前线程、关中断、调度器已锁定的调用。
检查外设状态时持有调度器锁，直到休眠返回或关机提交，防止另一个线程启动外设：

| blockers 位 | 阻止原因 | 处理 |
| --- | --- | --- |
| 01 | 蓝牙控制器线程已创建 | 当前 RF/控制器不能完整退出，需重启后再测试电源功能；关闭广播不等于释放控制器 |
| 02 | 音频打开、播放/DMA 或锁正在使用 | 停止播放并关闭音频设备 |
| 04 | FM 运行或操作未结束 | fmrx stop，等待结束 |
| 08 | USB CDC 运行或启停请求未完成 | usbcdc stop，等待 status 显示已停止 |
| 10 | TF 已探测、已打开或操作未结束 | 正常关闭文件、tf umount、tf stop |
| 20 | 可选 AUDEC 驱动仍有解码会话或操作 | 由会话所有者执行 `AB32_AUDEC_END` |
| 40 | SPI 总线锁正在使用 | 等待传输结束并释放总线锁，见 [SPI_I2C.md](SPI_I2C.md) |

不会自动中断蓝牙连接、强制卸载文件系统或丢弃音频队列。IR 和 GPIO/PWM 在浅休眠中保留，
其处理会在睡眠返回后继续；关机后则经过正常初始化恢复。
这些检查覆盖当前综合工程中的驱动；新增 DMA/存储/外设后需扩展检查，不能默认具备系统挂起能力。

### 软关机和硬件测试

```text
pmctl off              # 默认由 PWKEY 唤醒
pmctl off pwkey
pmctl off vusb
pmctl off both
```

OFF 成功后不返回命令行。选择 PWKEY 时，要求数字输入已使能、未处于模拟模式、按键已松开
（电平为高）。V2.0 可在上述 PWKEY 引脚与 GND 之间接一个常开瞬时按键；它不是板载 S2。
选择 VUSB 时，要求芯片检测到的 VUSB 当前离线，否则拒绝关机，防止立即唤醒。
需要保留 RTC 域供电：若拔掉唯一电源，再插回 USB，只能说明冷启动成功，不能证明关机唤醒。

推荐验证顺序：先查看 status，短按外接 PWKEY 确认电平变化；测试多次 sleep，核对串口、
tick、IR 和按键继续运行；最后测试 OFF 后由 PWKEY/VUSB 重新启动，并测量电流。
测试时保留人工断电重启的恢复途径。本次按用户要求只测试浅休眠，没有执行开发板软关机。

## 设备接口和错误处理

包含 [drv_power.h](bsp/libraries/hal_drivers/drv_power.h)，通过 rt_device_find("power0")
和 rt_device_control() 在线程中调用：

| control | 参数 |
| --- | --- |
| AB32_POWER_GET_STATUS | struct ab32_power_status * |
| AB32_POWER_SET_WAKE | struct ab32_power_wake *，mask/enabled 支持 PWKEY、VUSB |
| AB32_POWER_SET_INPUT | struct ab32_power_input *，enabled/pullup 为 0 或 1 |
| AB32_POWER_SET_FILTER | rt_uint32_t *，0/8/32/128/512 ms |
| AB32_POWER_SLEEP | NULL；RTC 秒对齐后睡眠到下一秒，补偿 tick，通常 1–2 秒返回 |
| AB32_POWER_OFF | struct ab32_power_off *，wake_sources 为非零合法掩码；成功不返回 |

PM 与 RTC 共用一把静态互斥锁及有界 SRAM 寄存器桥；每字节最多轮询 1024 次。
状态读取失败不发布部分结果；普通配置带掩码更新并读回验证。
普通配置不能改电源轨、RTC 时间或长按复位策略。

关机事务分为两个阶段：

1. 读取/校验状态，临时禁用唤醒复位，关闭旧唤醒使能以重新准备事件，设置 LDO、关闭 LVD，
   每步读回校验；失败则尝试恢复全部已涉及的配置及桥策略。桥持续故障时可能无法恢复，
   返回 -RT_EIO，不能承诺所有失败都已回滚。重新准备唤醒可能清除旧 WK/VUSB 事件，无法回滚该事件。
2. 最后重新检查输入，关中断，设置唤醒复位，提交关电源轨和指定唤醒源，写入 LPMCON。
   此路径及全部被调函数都留 SRAM。最终传输超时可能已经生效，因此触发看门狗复位，
   **绝不恢复线程运行**。成功后也不再读回、不打印或访问 Flash，等待断电/唤醒复位。

不修改秒计数、RTC RAM、首次上电标志或 CON9；软关机的准备路径只额外允许 CON0 的 LVD、
CON3 的唤醒策略和 CON5 的模式位。只在终端提交函数中允许清除 CON3 的电源使能。
状态中的 sleep_supported/poweroff_supported 表示代码入口已实现，不表示已经测量验证。

## 软件验证和资源

```sh
python3 tools/rtt/test_rtc.py
python3 tools/rtt/check_builds.py power power_external power_integrated led
python3 tools/rtt/memory_report.py
```

模型直接编译真实驱动，启用 UBSan，覆盖内部/外部 RTC 时钟、无 PM/带 PM 四种组合。
除原 RTC/配置测试外，新增浅休眠的 tick/上下文检查、五类外设忙状态、启动标志快照、
唤醒源及电平校验、准备阶段所有传输字节的永久/暂时故障、恢复失败、读回不一致、
临提交按键变化，以及最后命令/数据字节超时后必须进入复位路径的测试。
新增 RTC 停钟拒绝、对齐期间外设启动、提前唤醒、唤醒后读取失败的状态恢复，以及 tick 回绕测试。
模型本身不证明真实停钟；本次增加了以下实板验证。

综合版本原始堆 **23360 字节**，保持 22 KiB 下限，SRAM1 余 8 字节。
将仅在线程中使用的 btctl/diagnose 移到 Flash，为 RTC 唤醒临界路径留出 SRAM；控制器及 ISR 仍留 SRAM。
没有新增线程、动态缓冲，也没有削减线程/IRQ 栈或蓝牙固定区。
实测 allocator total=23272、used=9376、available=13896 字节，10 轮前后相同。

### 2026-11-03 V2.0 实板结果

- 最终 DCF 233560 字节；下载、CRC 校验及首块提交通过。
- 连续 10 次返回值均为 0、entries 均为 1，未发生重启或无法返回。
- 每轮 tick 增量为 1291–1838，电脑测得 1.301–1.848 秒，差约 9–11 毫秒（包含回复输出时间）。
- 整轮 RTC 前进 20 秒，电脑测得 19.530 秒，符合整数秒采样误差。未改 RTC 时间或清首次上电标志。
- 每轮状态均为 blockers=00、rails=7；末尾串口、线程查询正常，按键 read_errors=0、LED error=0。
- 用户确认唤醒后呼吸正常；最终按 S2 后保持熄灭。串口累计记录 4 次切换，最终 active=0、error=0，按键无读取错误。
- 未测电流，不能由成功返回推导具体低功耗指标；未执行 off。

可重复运行（只测睡眠，不会执行软关机）：

```sh
python3 tools/rtt/test_power_uart.py --port /dev/ttyUSB0 --cycles 10 --output /tmp/power-uart.json
```

详见 [MEMORY.md](MEMORY.md)、[实板记录](../docs/lib-analysis/generated/recovery/power-rtt-hardware.json) 和
[软件验证记录](../docs/lib-analysis/generated/recovery/power-rtt-software.json)。
