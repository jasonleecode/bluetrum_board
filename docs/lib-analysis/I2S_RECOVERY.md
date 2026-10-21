# I2S 硬件输出恢复与 RT-Thread 接入

2026-10-21。本次新增证据来自另一份 AB53XX SDK，而不是本仓库原有的
`libhal.a`。已恢复 **SRC 音源、主机、16 位双声道、标准 I2S、MCLK=256fs**
的初始化/退出子集，接入 RT-Thread `sound0` 播放路径。
这是同系列代码的实验性移植，尚无 AB32VG1 实机波形、采样率或收发验证。

## 来源与兼容性依据

公开镜像 [ZhiqingLi/Sdk_Refresh](https://github.com/ZhiqingLi/Sdk_Refresh/tree/aa0fc8f993bd0c2a6982bf0a95b3461ad7e7aa6d/AB53XX_SDK)
固定提交 `aa0fc8f993bd0c2a6982bf0a95b3461ad7e7aa6d`：

- `app/platform/libs/libplatform.a`：SHA-256
  `6165f17c6c25d58f23fb4732206fedacc085861c993d0607469c0368fb649409`。
- 其中 `i2s.o`：SHA-256
  `e14bfb1fe6831b5dd371500de6d209e700475284cd5e492df23b44cce7f2face`。
- DWARF 编译单元路径为 `AB5300/SDK/branches/sdk_v06x/libs/libplatform/i2s/i2s.c`，
  架构 RV32IMAC、ILP32；包含 `i2s_init/exit/isr/process`。
- `app/projects/standard/config.h` 的 I2S 配置说明给出主从、16/32 位、
  left-justified/normal、SRC/DMA、MCLK 比例和 PCM 模式。
- `app/platform/header/sfr.h` 的 GPIOA/GPIOE、FUNCMCON2、DACDIGCON0 地址
  与本板头文件对应；I2S PA0..4 / PE0..4 信号顺序与本仓库
  [AB32VG1_DataSheet.pdf](../ab32vg1_doc/AB32VG1_DataSheet.pdf) 一致。
  本板还定义 `RCU_IIS` 和 IRQ 17。

这些对应关系支持移植尝试，但**不能证明两个芯片所有寄存器兼容**：例如两份
SDK 的 RTC 命令定义不同。AB32VG1 公开寄存器手册未给出 IIS 控制器完整位域。
没有把整份旧 SDK 或其闭源库链接进工程，也没有将名称相似当作芯片验证。

## 恢复范围

实现：[i2s_src_recovered.c](../../bare/drivers/src/i2s_src_recovered.c)，
接口：[i2s_src.h](../../bare/drivers/inc/i2s_src.h)。

对应原 `i2s_init` 配置首字节 `0xa8 | mapping`，第二字节 0，mapping 为 0/1：

| 地址 | 原对象中的行为 |
| --- | --- |
| `0x024` | 写 3 清 I2S 映射字段，再写 1（PA）或 2（PE） |
| GPIO `+0x10/+0x14/+0x0c` | DE/FEN 设置 bits 1..4；DIR 清 bits 0..4 再置 bit0 |
| `0x140` | DACDIGCON0 置 bit23，选择 SRC 音源路径 |
| `0x4d8` | 先写 0，最后写 `0x609`，即当前固定输出配置 |
| `0x4dc` | 16 位时钟表索引 2 是 `{1,7}`，写入 `1 | (7 << 7)` |

退出保留原对象的 GPIO DE/DIR 和 `0x4d8=0` 序列。RT-Thread 适配另外
关闭 `RCU_IIS`、清 DACDIGCON0 bit23，释放共享音源；这些是接入策略，
不宣称为原对象逐指令恢复的部分。

本次没有启用原对象 DMA、中断、接收路径。其原始 ISR 存在循环缓冲计数、
未公开的 pending 操作及 DAC 队列依赖；公开 `bsp_i2s.c` 的结构声明还与
对象实际字段偏移不同。后续需要分别恢复、差分测试和验证缓冲区所有权。

## RT-Thread 使用

启用 `BSP_USING_I2S`，自动选择音频框架。`BSP_I2S_MAPPING` 默认 1：

| mapping | 数据输出 DO | BCLK | LRCLK | MCLK | DI（本次不使用） |
| --- | --- | --- | --- | --- | --- |
| 0 | PA1 | PA2 | PA3 | PA4 | PA0 |
| 1 | PE1 | PE2 | PE3 | PE4 | PE0 |

向 `sound0` 写入交错排列的 16 位双声道 PCM，可选 44100/48000 Hz，
同时送入片内 DAC 与 I2S。启动/停止由音频设备操作控制；不新增虚假的接收设备。
启用 I2S 时拒绝单声道配置；外部 codec 的寄存器初始化由应用负责。

PE 组使用的 PE1 也接板载 LED；此配置下 `main.c` 停止 GPIO 闪灯。
PA 组与 SPI1 mapping 1 冲突会编译报错；PE 组不能同时启用原来的软件 I2C1。
SPI 挂接还会拒绝占用 I2S 引脚的 CS。其它 PWM/UART/外接电路占用须按原理图选择。
GPIO 上的板载负载及走线可能影响信号，必须实测电平和时序。

FM 的 38 kHz 收音走片内 DAC，不启动 I2S；FM 与 PCM 互斥。
FM 停止后 PCM 可重新打开并启动 I2S。

## 验证

[test_i2s_src_recovery.py](test_i2s_src_recovery.py) 将原对象与恢复 C 编译为
RV32 ELF，在 Unicorn 下比较有序 MMIO 读写及最终寄存器状态。
136 组测试覆盖两组引脚、4 种固定及 64 种随机初始状态，每组两次初始化/退出；
另外检查 3 个非法映射不会访问 MMIO。SRC 路径若意外调用 DMA/IRQ 辅助函数，测试失败。
结果见 [i2s-src-differential.json](generated/recovery/i2s-src-differential.json)。

```sh
PYTHONPATH=/tmp/ab32-recovery-deps python3 docs/lib-analysis/test_i2s_src_recovery.py \
  --archive /path/to/AB53XX_SDK/app/platform/libs/libplatform.a \
  --linker /path/to/ld.lld
python3 tools/rtt/test_drivers.py
python3 tools/rtt/check_builds.py spi_fm_i2s i2s_pa
```

模型与编译检查通过不等于硅片时序通过；I2S 仍为默认关闭的实验性功能。

## 硬件 I2C 的补充核查

同一固定提交的 `app/platform/bsp/bsp_i2c.c` 仍使用 `I2C_SCL_H/L`、
`I2C_SDA_H/L` 和 `delay_us(5)` 逐位传输；`sfr.h` 未提供 I2C/IIC 控制器，
新下载的 `libdrivers.a/libplatform.a` 也未发现相应命名符号。
因此本次仍不能交付硬件 I2C。原先的阴性结果不再延伸为“I2S 也没有对象”：
本次确实找到了 I2S 新证据，硬件 I2C 则仍需独立的寄存器或固件证据。
