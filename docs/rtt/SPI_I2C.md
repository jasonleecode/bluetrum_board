# SPI / I2C 接入（AB32VG1 V2.0）

综合工程默认注册硬件 `spi1`；只有应用配置或运行 `spi1check` 时才配置引脚和启动传输。
驱动为恢复源码，不链接原厂 `driver_spi.o`。硬件 I2C 仍缺少可确认的控制器定义，未实现。

## 引脚和共存

默认 `BSP_SPI1_MAPPING=6`，来自芯片数据手册的 **Pinouts and pin description** 表：

| SPI 信号 | 芯片引脚 | V2.0 排针 | 注意 |
| --- | --- | --- | --- |
| DI / MISO | PE4 | IOH 第 8 脚 | P1 的 7–8 跳线连接绿色 LED，接外设时应断开 |
| CLK / SCK | PE3 | IOH 第 1 脚，丝印 SCL/CLK | 与 IIC 时钟共用 |
| DO / MOSI | PE2 | IOH 第 2 脚，丝印 SDA | 与 IIC 数据共用 |
| CS | 应用选择空闲 GPIO | 按接线指定 | 例如 PA0；不是自动硬件 NSS |

见 [芯片手册](../ab32vg1_doc/AB32VG1_DataSheet.pdf) 和
[V02 原理图](../ab32vg1_doc/AB32VG1_Prougen_Schematic_V02.pdf)。
第 6 组的引脚顺序与旧库中的 1–5 组不同：PE4 输入，PE3/PE2 输出。
它避开默认固件的 UART0、按键、TF、红外和红色呼吸灯，但不能与 PE 组 I2S 或
使用 PE2/PE3 的 I2C 同时启用。源代码对这些冲突以及 UART0/1/2 的对应 SPI 组做编译拒绝；
片选挂接检查默认综合功能已使用的引脚。其他自定义外设仍需应用自行规划引脚。

## RTT 接口

```c
#include "drv_spi.h"
static struct rt_spi_device sensor; /* 挂接期间必须一直存活 */

/* 在线程中调用，保持中断和调度开启。CS 仅为接线示例。 */
rt_err_t rc = ab32_spi1_attach(&sensor, "sensor", rt_pin_get("PA.0"), 1000000);
/* rc == RT_EOK 后，按外设协议调用 rt_spi_send / rt_spi_recv /
 * rt_spi_send_then_recv。 */
```

支持主机、8 位 MSB、独立收发阶段、GPIO 片选；不使用 GPIO 逐位移位。
保留厂商复位时序，`RT_SPI_MODE_0` 只是当前 API 入口，尚未用波形确认 CPOL/CPHA。
其他时序、LSB、从机和同时 TX+RX 返回不支持，不以两次传输模拟一次全双工。
分频采用向上取整，暂保守限制为 1..256；频率参数是最大目标值，不能用软件配置值
代替测得的线速。硬件 BAUD 读回为零，诊断中分别显示写入值、原始读回值和计算目标值。

超时按真实 RTT tick 且支持回绕；任意字节失败均以负值返回，避免 RTT 的链式传输
把部分发送误认为完整成功。超时关闭引擎、释放片选并使总线配置缓存失效，下一次传输重配。
共享 GPIO 修改在 SRAM 临界区完成。SPI 总线锁持有期间，`pmctl sleep/off` 被拒绝，
`blockers` 的 `0x40` 表示 SPI 操作；应用手动使用 `rt_spi_take/release` 时须同时持有总线锁。

## 无外设自检

确保 PE2/PE3/PE4 没有连接外部模块，使用烧录串口执行：

```text
spi1check 1000000
```

命令临时挂接无片选测试设备，发送 16 字节 `AA/55`，分别启用 DI 下拉/上拉，
检查 16 字节全 `00` / 全 `FF`，然后统计 4096 字节发送的 RTT tick 耗时。
完成后注销临时设备并恢复相关引脚配置、停止 SPI，不保留测试堆分配。
已有总线配置所有者时拒绝测试，以免打断其他 SPI 应用。

这个测试验证控制器完成、DI 输入选择和重复传输。它**不能**验证 MOSI 的实际数据、
SCK 电气波形、CPOL/CPHA、从设备应答、真实 CS 时序或全双工。
后续需用已知 SPI 外设及逻辑分析仪/示波器完成这些验证。

### 2026-11-03 V2.0 实测

综合固件已经 CH340 下载、CRC 校验并启动。4 轮上下拉接收全部符合预期：

| 请求频率 | 按 SDK 公式计算的目标频率 | 4096 字节发送耗时 |
| --- | --- | --- |
| 200 kHz | 200 kHz | 198 ms |
| 1 MHz | 1 MHz | 56 ms |
| 5 MHz | 4.8 MHz | 29 ms |
| 200 kHz（RTC 休眠恢复后） | 200 kHz | 199 ms |

耗时包含框架调用、轮询和调度开销，只验证分频设置影响传输速度，不能当作波形频率测量。
各轮 BAUD 均读回零，故不再用读回值报告“实际速率”。`spi1check 0` 正确拒绝，
按键 `read_errors=0`、电源 `blockers=00`，测试前后堆均为 total=18760、used=9376、available=9384。
原始串口输出及固件 SHA-256 见 [实板报告](../lib-analysis/generated/recovery/spi-rtt-hardware.json)。

复现（会执行一次 RTC 浅休眠，要求空排针、未启动蓝牙/FM/音频等 PM 阻止项）：

```sh
python3 tools/rtt/test_spi_uart.py --port /dev/ttyUSB0 --output /tmp/spi-report.json
```

软件回归：

```sh
python3 tools/rtt/test_drivers.py
python3 tools/rtt/test_spi_config.py
python3 tools/rtt/check_builds.py default spi_fm_i2s
python3 tools/rtt/memory_report.py
```

模型覆盖 G4/G6 方向、接收、片选、部分传输超时、tick 回绕、非法上下文和 PM 忙状态；
编译检查覆盖 G6 与 UART/I2S/IIC 引脚冲突及旧 G4+FM+I2S 配置。

## PE2/SDA、PE3/SCL 与硬件 I2C

板上丝印和原理图确实将 PE2/PE3 用作 IIC 接口，厂商 BSP 默认也是
SDA=RT pin 15（PE2）、SCL=RT pin 16（PE3）。但接口用途不等于片上控制器类型。
厂商 [官方 BSP 外设表](https://github.com/BLUETRUM/sdk-bsp-ab32vg1-ab-prougen/blob/master/rtt_default_project_0/README.md)
明确注明 **软件 I2C**；源码 `drv_soft_i2c.c` 使用 RT-Thread bitops。

现有芯片手册、寄存器头以及已审计的 SDK `.a` 中尚未找到可确认的硬件 I2C
寄存器、引脚复用选择或初始化/收发对象。历史检索范围和来源见
[SPI/I2C 恢复记录](../lib-analysis/SPI_I2C_RECOVERY.md)。
这不能证明硅片绝无控制器，但不足以实现可信的硬件驱动。
因此本次没有把 GPIO 模拟实现更名为硬件 I2C，也没有擅自启用旧的软件 I2C。
继续硬件恢复需要厂家完整寄存器说明或确实使用硬件 I2C 的 SDK/固件。
