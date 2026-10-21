# 硬件 SPI / I2C 逆向进展

2026-10-21。按要求只研究硬件控制器，本轮没有新增软件 SPI/I2C 实现。

## 已交付：硬件 SPI1 的 C 恢复实现

- 实现：[driver_spi_recovered.c](../../bare/drivers/src/driver_spi_recovered.c)
- ABI 头：[blue_spi.h](../../bare/drivers/inc/blue_spi.h)
- 原对象新反汇编：[driver_spi.llvm.txt](generated/recovery/driver_spi.llvm.txt)
- 对照测试：[test_spi_recovery.py](test_spi_recovery.py)
- 测试结果：[spi-differential.json](generated/recovery/spi-differential.json)

目标是 `bare/lib/libhal.a:driver_spi.o`，原对象 SHA-256：
`84e5ce25a4368d749c0efd9f08f19ba5ca9f44698d477ed59dbedefffd2564bd`。
它包含 914 字节可执行段，导出 `blue_spi1` 九项函数表：版本、初始化、反初始化、
电源控制、DMA 发送、DMA 接收、单字节发送、单字节接收、控制配置。
同时恢复内部的 GPIO 复用配置和中断处理函数。

时钟和数据移位由 SPI1 硬件完成；GPIO 操作只用于初始化引脚复用，没有逐位软件移位。
实现不调用原 `driver_spi.o`，也不依赖其私有函数。
平台仍需提供 `get_sysclk_nhz`、`hal_get_ticks`、`register_isr`、
`os_interrupt_enter`、`os_interrupt_leave`。

恢复采用保留原行为的策略，未将猜测的 CPOL/CPHA 位或硬件 I2C 寄存器加入实现。
下面的原厂边界行为也被保留并测试，不能将它直接当作做完参数保护的产品级 API。

## 硬件寄存器与控制流程

| 地址 | 名称 | 恢复出的操作 |
| --- | --- | --- |
| `0x020` | FUNCMCON1 | bits 15:12 选择 SPI1 引脚组；先写 `0xf` 清选择，再写组号 |
| `0x980` | SPI1CON | bit 0 使能，bit 1 从机选择，bit 2 simplex 配置，bit 4 接收选择，bit 7 DMA 路径使用，bit 16 pending |
| `0x984` | SPI1BUF | 写入启动单字节传输；接收函数写 `0xff` 后等待，再读结果 |
| `0x988` | SPI1BAUD | 主机模式写入 `get_sysclk_nhz() / baudrate - 1` |
| `0x98c` | SPI1CPND | 中断处理写 `0x10000` 清 pending |
| `0x990` | SPI1DMACNT | 直接写 `num`，此库没有减一 |
| `0x994` | SPI1DMAADR | DMA 缓冲区地址 |

寄存器名与地址由本仓库 `bmsis/include/ab32vg1.h` 交叉核对。
bit 7 的启用/清除来自代码观察；尚不将它断言为手册确认的独立 DMA 使能位，
它可能包含中断控制意义，需硬件/完整位域手册验证。

`FUNCMCON` 的写入不是普通 RAM 位域赋值。公开寄存器文档中映射字段的 `0xf`
用于清选择，`0` 不改变选择；官方 `gpio_afinit` 也采用先 OR 清码、再 OR 映射的两步。
不能把原库中看似奇怪的 `| 0xf000` 随意改成普通清零操作。

| 组号 | 数据输入 DI | 时钟 CLK | 数据输出 DO |
| --- | --- | --- | --- |
| 1 | PA2 | PA3 | PA4 |
| 2 | PA5 | PA6 | PA7 |
| 3 | PB0 | PB1 | PB2 |
| 4 | PE5 | PE6 | PE7 |
| 5 | PF0 | PF1 | PF2 |

以上来自跳转表重定位和 `spi_io_mapping` 参数，亦与随附 `driver_spi.h` 一致。
该对象没有自动 CS 管理，应用需控制片选；具体板上引脚占用仍需接线时检查。

关键路径及节内偏移：

- `.text+0x90`：主机命令 `1`、从机命令 `2`、主机 simplex 命令 `3`，注册中断向量 `20`。
- `.text+0x110`：映射命令 `0x20`；`.text+0x204`：使能命令 `0x21`。
- `.com_text.spi+0x0`：发送 DMA，写地址 → 计数 → 控制位，立即返回。
- `.com_text.spi+0x26`：接收 DMA，写控制位 → 地址 → 计数，立即返回。
- `.com_text.spi+0x4c`：进入 OS 中断钩子，检查 pending，清 pending，清 `0x90`，回调事件 `1`，离开中断。
- `.com_text.spi+0x9a` / `+0xf4`：轮询接收/发送一个字节，超时返回 `-1`。

## 保留的原厂行为及未解决项

- `power()` 什么也不做而返回成功；`MODE_INACTIVE` 也不关闭控制器。关闭应调用 `deinit()`。
- 随附头文件宣称 `MODE_SLAVE_SIMPLEX=4`，但该对象会清控制寄存器后返回 `-1`。
- 非法映射值仍会清复用/配置状态并返回成功；调用方只能传 `1..5`。
- DMA 函数没有忙状态、空指针、零计数保护。等待事件 `1` 前不要释放缓冲区或开始另一传输。
- 单字节路径没有显式写 CPND；按原库行为保留，硬件如何清旧 pending 尚需验证。
- 超时使用绝对截止 tick 比较，存在计数回绕问题。`timeout <= 0` 等待无限，
  signed 32-bit 比较来自机器码；历史头声明 `uint16_t`，两者不完全一致。
- 当前裸机 shim 的 `hal_get_ticks()` 是每次调用递增，不是真实毫秒；
  `get_sysclk_nhz()` 固定返回 48 MHz。上板前必须提供真实时钟/时间基准。
- 当前裸机中断入口/分发尚是占位实现；注册函数指针不等于中断能实际到达，
  DMA 完成回调需要先接通中断系统。单字节轮询不依赖 DMA 回调。
- `drv_spi.c` 现有上层仍忽略 mode，且 transfer 由 send_byte+recv_byte 组成，
  会启动两次传输；它不能作为已经验证的同一组时钟全双工接口。
  本轮只统一其 ABI 头，没有宣称这些上层问题已修复。

官方 [SPI 功能说明](https://ab32vg1-datasheet.readthedocs.io/en/latest/functional-description/serial-peripheral-interface.html)
描述了四种时序、SPIBUF 启动传输以及 DMA 流程，但未给出完整位域。
因此不能由当前库只使用少数控制位，推断硬件不支持其他时序；也不能猜出 CPOL/CPHA 位。
本轮仅恢复该对象确实实现的接口。

## 验证与接入

通过 34 组差分测试：将原对象和恢复 C 的 RV32IMAC 编译结果分别加载到 Unicorn，
在同一 MMIO 模型下比较每一步寄存器读取/写入、平台钩子、回调和 API 返回值。
覆盖三种角色 × 五组映射、DMA 收发/中断、字节收发、禁用态、非法控制值、
零/负超时、正常超时、tick 回绕、零速率和零 DMA 长度。

这不是上板测试，也不是所有输入的形式化等价证明。MMIO 模型只规定共同的模拟行为。
厂商 `__riscv_restore_0/3` 使用 Unicorn 不支持的自定义指令；测试依据配对 save
函数的栈布局模拟其恢复 ABI。SPI 函数体与 save 指令实际执行；未验证自定义指令本身。

还通过了包装层与恢复驱动的链接检查：在五个平台钩子使用地址桩的条件下，
即使将原 `libhal.a` 列为可用库，也没有抽取其中任何成员。此检查证明这条 SPI
实现不再依赖黑盒驱动，不代表其他模块已脱离黑盒库。

Makefile 默认仍选原库，显式切换使用：

```sh
make -C bare SPI_BACKEND=recovered
```

它把 C 实现链接到原库之前，产物放入 `bare/build/recovered-spi/`，与默认构建隔离。
本机没有 Makefile 默认的 `riscv64-unknown-elf-gcc`；本轮实际运行的是 Clang
交叉编译、LLD 单模块链接、两种 Makefile 路径 dry-run，未完成整套固件 GCC 构建。

复现测试需要 Unicorn 2.1.4、pyelftools 0.33、Clang 和支持 RISC-V 的 LLD：

```sh
python3 docs/lib-analysis/audit_archives.py
python3 docs/lib-analysis/test_spi_recovery.py --linker /path/to/ld.lld
```

本轮 Python 测试依赖装于 `/tmp/ab32-recovery-deps`，运行时使用
`PYTHONPATH=/tmp/ab32-recovery-deps`，没有修改系统 Python 安装。
LLD 使用已有 Rust 工具链中的 `lib/rustlib/x86_64-unknown-linux-gnu/bin/gcc-ld/ld.lld`。

## 硬件 I2C：尚未找到可恢复的对象

“未找到”表示缺少已确认的硬件代码入口和寄存器定义，不表示已经证明硅片没有控制器。
本轮没有生成名为硬件 I2C、内部却使用 GPIO 模拟的驱动。

已检查以下资料：

1. 本仓库两个库、SPI 测试目录的库、BLE ZIP 内三个库、SPI 7z 内一个库。
2. 厂商官方 `BLUETRUM/bluetrum_sdk` 固定提交
   `2c08bb64173fda3e8da26bbc52242a7708a3edc3` 的 `libhal.a`。
3. 本地寄存器头、HAL/驱动源代码、芯片数据手册、寄存器手册、开发板手册。
4. 厂商 `bluetrum_sdk_old` 的完整 Git 树
   `d50e61c77906cba86cb033289a5344a8b6851e20`，以及当前 SDK Git 树。

8 个库位置、5 种不同库内容的 `nm -a` 搜索均无 `i2c/iic/twi` 名称命中。
具体来源、SHA-256、成员列表在 [i2c-search.json](generated/recovery/i2c-search.json)。
这是名称检索的阴性结果，无法排除无符号/改名的实现。

本地 `rtt/bsp/board/Kconfig` 明确写 `Enable I2C1 BUS (software simulation)`，
选用 `RT_USING_I2C_BITOPS`；测试包同样提供 `drv_soft_i2c.c`。
开发板手册描述“一路 IIC 接口”，而芯片数据手册的外设列表/引脚复用没有对应
I2C 控制器信息。连接器用途与片上控制器能力不能混为一谈。
官方 [功能目录](https://ab32vg1-datasheet.readthedocs.io/en/latest/)
也没有 I2C 专章；目录缺项本身仍不能证明硬件不存在。

下一步能真正推进硬件 I2C 的证据是：完整 I2C 寄存器/复用说明，或含硬件 I2C
初始化及收发实现的 SDK/固件。有这样的对象，才能沿调用、寄存器写入、中断和状态机恢复；
当前不能凭空交付可信的硬件 I2C 驱动，也不能承诺仅凭现有两份 `.a` 一定能恢复它。
