# PWM 与 S2 呼吸灯

综合 RTT 默认启用 `BSP_USING_LED_BREATH` 和 Timer5 PWM0，替代原 PE1 心跳闪烁。
V2.0 的红色 LED 为共阳极、低电平点亮；**P1 的 3–4 脚须短接**。
PE1 对应 Timer5 **PWM0**，旧 Kconfig 中写成 PWM2 的描述已修正。
依据：[V02 原理图](../ab32vg1_doc/AB32VG1_Prougen_Schematic_V02.pdf)、
[数据手册引脚表](../ab32vg1_doc/AB32VG1_DataSheet.pdf)、
[寄存器手册第 2、4 章](../ab32vg1_doc/AB32VG1_Register.pdf)。

## 操作

- 上电红灯熄灭；S2/PF1 去抖后的按下事件切换呼吸开关。
- 第一次按 S2：从熄灭逐渐变亮再变暗，约 3 秒一周期；再按：停止并熄灭。
- 按住 S2 只切换一次，LONG 和松开不再切换；S3/S4 不控制红灯。
- `led status` 显示 ready、active、亮度（0..1000）、切换次数和错误码。
- `keys read/watch` 仍能读取全部按键事件，呼吸灯不消费 `keys0` 队列。

4 kHz 载波由 Timer5 产生；现有 main 每 10 ms 更新一次平方亮度曲线，无新线程、IRQ 或动态缓冲。
0% 和 100% 亮度由 GPIO 明确输出高/低电平，中间亮度切换为硬件 PWM。
初始化或运行错误会熄灭红灯并记录 error。`led status` 为只读诊断命令。

## 驱动修复与 API 边界

[drv_pwm.c](../../rtt/bsp/libraries/hal_drivers/drv_pwm.c) 已修复以下问题：

- 开启 Timer3/4/5 外设时钟；初始化清除计数和 pending，关闭捕获与 IRQ；
  enable 同时启动计数器，最后一个输出 disable 后停表。
- 修复 MSP 中 Timer/LPWM 引脚复用的清位掩码，避免误改其他功能的复用字段。
- 纳秒换算使用 64 位乘法。移除只在计算中存在、并未配置到硬件的假分频；
  不能表示的周期返回错误，不悄悄改变频率。
- Timer 的 DUT 编码是低电平长度减一；RT PWM 的 pulse 表示高电平时长，明确换算极性。
- 校验空参数、通道、周期、脉宽、上下文；所有设备共用静态互斥锁，拒绝 ISR/无当前线程调用。
- 各 Timer 通道分别保存脉宽；LPWM 两个通道共用一个 duty 寄存器，更新时保留另半字。
  已配置的其他通道要求相同周期，跨通道改变公共周期返回 `-RT_EBUSY`，即使输出暂时关闭。
- 未实现互补输出，明确返回 `-RT_ENOSYS`。修复 PWM shell 的 `get` 缺参检查、
  错误的设备名指针打印及占空比除零，改用整数计算。

沿用此 BSP 的通道编号：Timer 通道 **1/2/4 对应 PWM0/1/2**，只接受一个通道，
不接受组合掩码；每个 `lpwmN` 设备只接受 channel=1。默认只注册 `t5pwm` 的 channel=1。
`rt_pwm_set()` 的 period/pulse 单位为 ns，GET 返回已接受的请求值，硬件按时钟 tick 舍入。
支持 SET、GET、ENABLE、DISABLE、SET_PERIOD 和 SET_PULSE。

Timer 路径按系统时钟计数；为适配 16 位 DUT，本驱动接受 2..65536 个 tick 的周期。
Timer 的 pulse 必须能舍入为 1..period_ticks−1；0%/100% 直流输出由应用切回 GPIO，
通用驱动明确拒绝这些端点。LPWM 沿用原 BSP 的 6.5 MHz 时钟假设和手册 DUT/PR 比例，
允许端点；其真实频率/波形未验证。修改系统时钟后应重新配置 PWM。

Timer5 与 hwtimer5、I2S PE1 不能同时占用；Timer3 与 IR/TF 等冲突也有编译检查。
PWM/呼吸灯代码只在线程上下文运行，放 Flash；GPIO 临界代码继续留 SRAM。
呼吸灯运行时由 main 独占 PE1，勿同时用 `pwm set/enable/disable` 手动改写 t5pwm。

## 软件验证

```sh
python3 tools/rtt/test_pwm.py
python3 tools/rtt/test_keys.py
python3 tools/rtt/check_builds.py led pwm rtc_integrated i2s_pa spi_fm_i2s
python3 tools/rtt/memory_report.py
```

模型直接编译真实 HAL、PWM 驱动、RT PWM 框架、按键和呼吸灯代码，使用 UBSan。
覆盖启动/停止、时钟/复用字段保留、极性、非法参数不写寄存器、LPWM 相邻通道保持、共享周期、
互斥/ISR 错误，以及 S2 抖动/长按/开关、保留队列、S3 不触发、亮度端点、tick 回绕和故障熄灭。
这些测试不能替代示波器频率测量或肉眼观察。

## V2.0 实板验证（2026-11-02）

综合固件已通过 CH340 烧录，228352 字节镜像正文写入及 CRC 校验成功。
用户确认红灯平滑变亮/变暗，并确认再次短按 S2 后完全熄灭。
串口观察到 ready=1、error=0，累计切换次数从 3 增至 5；最后一次读取时 active=1，
即当时已再次处于呼吸状态。肉眼停止确认与随后串口快照分别记录，不将后者写成熄灭状态。

`keys read` 仍读到 S2 DOWN/UP；较晚读取时 8 条队列已覆盖两条旧事件，dropped=2，
符合队列丢最旧事件的策略，read_errors=0。两次 `free` 均为 total=22536、used=9376、
available=13160；RTC 仍递增。PWM shell 缺参返回 -22，正常 GET 可读取 250000 ns 配置。

记录及哈希见 [实板报告](../lib-analysis/generated/recovery/pwm-led-rtt-hardware.json)。
4 kHz 为配置目标，未使用示波器测量；其他 Timer/LPWM 波形和多外设同时运行未做硬件验证。
