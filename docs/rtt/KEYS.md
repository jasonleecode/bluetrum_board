# 三个板载按键

综合 RTT 默认启用 `BSP_USING_KEYS`，通过现有 PIN 驱动接入 `keys0`。
依据 [V02 原理图](../ab32vg1_doc/AB32VG1_Prougen_Schematic_V02.pdf)，接线为：

| 事件编号 | 板上按键 | GPIO | RTT pin 编号 |
| --- | --- | --- | ---: |
| KEY1 | S2 | PF1 | 22 |
| KEY2 | S3 | PF0 | 21 |
| KEY3 | S4 | PA2 | 2 |

三个键均按下接地，驱动配置数字输入和内部上拉，无需外接按键。
S1 是电源复位，不属于上述三个功能键。旧板资源列表中的“两个 USER 加 RESET”
不能代替 V02 的实际接线；使用其他版本需先核对原理图。

## 串口使用

烧录本版综合固件后，在原 CH340/UART0 控制台（115200、8N1）执行：

```text
keys status
keys watch 10
keys read
```

`watch` 默认观察 10 秒，可指定 1–30 秒，期间逐个短按、长按或同时按下 S2/S3/S4。
每键输出 `DOWN`、`UP`，按住约 800 ms 后输出一次 `LONG`；持续按住不重复输出 LONG。
`held` 从消抖后的 DOWN 起计时，`tick` 为系统 tick。`read` 非阻塞取走队列已有事件。
`status` 的 raw/pressed 掩码 bit0/1/2 对应 KEY1/2/3，分别表示原始和消抖后的按下状态；
同时报告 queued、dropped、read_errors。

扫描在现有 `main` 线程每 10 ms 执行，按下和松开均要求稳定 30 ms，
实际响应受线程调度影响。PE1 红灯继续每 500 ms 翻转。没有新增按键线程、定时器或中断，
也未绑定 FM 调台、音量或蓝牙操作，应用可消费事件实现这些功能。

## 应用接口

头文件为 [board_keys.h](../../rtt/bsp/app/board_keys.h)，实现为 [board_keys.c](../../rtt/bsp/app/board_keys.c)。
设备上电自动注册并开始扫描，应用在线程中打开和读取：

```c
#include <rtdevice.h>
#include "board_keys.h"

rt_device_t dev = rt_device_find("keys0");
if (dev && rt_device_open(dev, RT_DEVICE_OFLAG_RDONLY) == RT_EOK) {
    struct ab32_key_event events[AB32_KEYS_QUEUE];
    rt_ssize_t bytes = rt_device_read(dev, 0, events, sizeof(events));
    /* bytes > 0 时处理 bytes / sizeof(events[0]) 项；0 表示当前无事件。 */
    struct ab32_keys_status state;
    rt_device_control(dev, AB32_KEYS_GET_STATUS, &state);
    rt_device_close(dev);
}
```

read 的位置必须为 0，长度和返回值以字节计，长度必须为完整事件记录的倍数。
每项 12 字节，静态队列 8 项，满时丢弃最旧事件并增加 dropped，当前按键状态仍更新。
shell 与应用共享同一队列，按单消费者设计；需要多处订阅时由应用统一分发。
上电时已经按住的键也会在消抖后产生 DOWN。没有双击、组合键语义或自动连发。

## 引脚与 GPIO 中断

KEY3/PA2 与 SPI1 G1、I2S G1（`BSP_I2S_MAPPING=0`）、LPWM2 G3 冲突；
KEY1/KEY2 与 SPI1 G5 冲突。Kconfig 和编译检查均拒绝这些组合。
PF0/PF1 也可作模拟 AUX 输入，启用未来录音功能时须重新分配；当前 DAC/FM 输出不占这些键。
应用不能在扫描期间把三个引脚改为输出或复用给其他功能。

[drv_gpio.c](../../rtt/bsp/libraries/hal_drivers/drv_gpio.c) 同时修复了共享唤醒线路覆盖回调、
detach 清错引脚、WKUPEDG 边沿位和 pending 清除位错误，以及误用全局中断恢复 API 的问题。
回调按 27 个有效引脚分别保存，配置改变保留其他已启用引脚；返回普通 GPIO 时清除 FEN，
输出使用 SET/CLR，避免读改写整个端口。

通用 GPIO 边沿回调通过 `BSP_USING_PIN_IRQ` 单独开启，**默认关闭**；关闭时
`rt_pin_attach_irq/rt_pin_irq_enable` 返回 `-RT_ENOSYS`，上述按键事件正常工作。
开启后支持上升、下降、双边沿，不支持高/低电平模式。首次 enable 按需分配 1024 字节栈
和线程对象；后续 disable 保留工作线程。attach、enable、detach 均在线程上下文调用，
用户回调在工作线程执行；detach/disable 等待正在执行的回调结束，允许回调自身 detach。
回调应短小，避免与其他线程形成互斥锁等待环。

共享 PORT_INT 只有线路 pending，没有逐引脚 pending；驱动采样电平变化、重设下一边沿，
重复事件会合并。这适合慢速状态通知，不保证捕获短脉冲，也不提供精确边沿计数或时间戳。
寄存器更新保留 RTC/WKO 的 bit5，但没有接入其共享 IRQ 分发或低功耗唤醒功能。

## 验证与内存

本版已下载至 CH340/CRWN 开发板，通过 Flash CRC 校验并启动 RT-Thread 5.2.2。
用户实际操作后，串口记录到 KEY1/KEY2/KEY3 的 DOWN/UP，以及 KEY2 在 800 ms 的 LONG，
随后 1590 ms 松开产生 UP。三个实体按键和一次长按已验证；通用 GPIO IRQ 仍只通过离线测试。
启动时可用堆 13064 字节，总堆 22440 字节。验证记录见
[按键实机结果](../lib-analysis/generated/recovery/keys-rtt-hardware.json)。

离线复测执行：

```sh
python3 tools/rtt/test_keys.py
python3 tools/rtt/check_builds.py default keys_irq ir_integrated peripherals i2s_pa
python3 tools/rtt/memory_report.py
```

主机测试直接编译实际 GPIO/HAL 和按键代码，使用模拟寄存器与 RTOS，启用 UBSan。
覆盖三键同时按下、两侧消抖、长按仅一次、溢出计数、读取错误、tick 回绕、非对齐缓冲、
观察超时、冲突配置拒绝，以及共享/专用 GPIO 中断、双边沿、W1C、回调期间的新中断、
解除/重新挂接和首次线程申请失败重试。它不验证真实电平、接触抖动波形或中断时序。

默认综合固件原始堆区为 22528 字节（22 KiB），SRAM1 余 32 字节；按键不申请动态队列或线程。
可选 GPIO IRQ 综合配置原始堆区为 21376 字节，启用回调时另从堆分配工作线程。
这些是链接结果，运行时余量和多模块共存仍需测量。详见 [MEMORY.md](MEMORY.md) 和
[软件验证记录](../lib-analysis/generated/recovery/keys-rtt-software.json)。
