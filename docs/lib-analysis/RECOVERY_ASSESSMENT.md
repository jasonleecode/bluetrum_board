# SDK 黑盒库逆向可行性与首轮实现恢复

日期：2026-10-21。对象：本仓库 `bare/lib/libhal.a`、`bare/lib/libbtctrl.a`。

## 结论

后续更新：`sdadc.o` 已移入可选固件后端并通过差分测试，原 ADC 封装的通道/DMA 参数错误已修复；本页保留首次评估记录，最新实现与限制见 [音频恢复记录](AUDIO_RECOVERY.md)。

可以继续逆向，已经从 `sdadc.o` 的机器码人工恢复出覆盖其 12 个导出函数的
[C 分析实现](recovered/sdadc_recovered.c)。它通过了 RV32IMAC/ILP32 交叉编译，
但没有经过板上验证或完整行为等价验证，尚不能作为正式驱动替换进固件。

这两个 `.a` 是普通静态库，成员是可直接读取的 RISC-V ELF32 可重定位对象，
并非必须先解密才能分析的容器。符号、函数大小、局部标签、重定位和常量均保留。
它们没有 DWARF 调试段；不能从中保证恢复原始注释、宏、局部变量名和原始 C 类型。
恢复可理解逻辑及逐模块重写具有可行性；本轮没有完成整个 SDK 的源码重建。

## 实测范围与证据

| 库 | 对象数 | 已定义 FUNC 符号数 | 可执行段字节数 | 全局/弱定义符号 | 含调试段的对象 |
| --- | ---: | ---: | ---: | ---: | ---: |
| libhal.a | 23 | 129 | 12,408 | 181 | 0 |
| libbtctrl.a | 32 | 374 | 43,400 | 324 | 0 |

FUNC 包含局部函数以及可能共享实现的别名，不等于独立源码函数数量。
全局定义还包括数据和绝对地址符号，不全是函数。

[archive-audit.json](generated/recovery/archive-audit.json) 保存每个库及成员的
SHA-256、函数位置与大小、ELF flags、重定位数量和历史反汇编的字节核对结果。

- 历史 GNU 反汇编共核对 17,334 行编码，全部与当前库字节一致。
- 覆盖 HAL 可执行段 12,064 / 12,408 字节、BT 可执行段 42,866 / 43,400 字节。
  未覆盖部分仍明确记为未检查；不能把这一检查称为完整指令语义验证。
- 本机 `llvm-objdump-10 -dr --mattr=+m,+a,+c` 对实际解包的 `sdadc.o` 做了新反汇编：
  [sdadc.llvm.txt](generated/recovery/sdadc.llvm.txt)。函数/全局引用同时参考原对象重定位。
- 新 C 的 12 个导出函数名称集合与原 `sdadc.o` 一致；编译输出为 ELF32 RISC-V、
  RVC、soft-float ABI。名称和编译通过不证明完整 ABI 或执行行为等价。

## 本轮恢复出的具体行为

下列偏移均为 `libhal.a:sdadc.o` 内的节相对偏移，不是最终固件地址。

| 入口/位置 | 机器码直接证据及推导 | 对现有项目的影响 |
| --- | --- | --- |
| `.text+0x0` `sdadc_set_channel` | 入参为零跳到返回 `-1`；否则存入配置首字节 | `DRV_ADC_CH_MIC = 0` 直接传入会初始化失败 |
| `.text+0x242` `sdadc_start` | 原始 `a0` 在 `+0x268` 或 `+0x302` 写入 `0x1cc` / `0x1d4`，此前无函数调用改写它 | 高可信推断它接收 DMA 地址；当前 `extern int sdadc_start(void)` 漏了参数 |
| `.text+0x262..0x26e`、`+0x302..0x308` | 从配置 `+4` 读半字，减一写 `0x1d0` / `0x1d8` | DMA 长度配置为零会写 `0xffffffff`；库自身未校验 |
| `.text+0x224` `sdadc_get_sample_rate` | 返回配置 `+1` 的字节 | 返回采样率索引 0..9，而非 Hz |
| `.text+0x1de` / `.text+0x28c` | 保存 16 位 gain；数字路径使用低 5 位，启动时模拟路径使用 `(gain >> 5) & 0xff` | 当前封装的 `uint8_t gain` 无法表达库中观察到的全部位域 |
| `.text+0x29c..0x2ac`、`+0x378..0x388` | 低半字节为 12 或 13 时调用 mic；仅高半字节模式以 `channel & 0xe0 == 0xc0` 选 mic | channel 是编码值，不是当前包装层的简单 MIC/AUX 枚举；具体引脚仍需追踪模拟层 |
| `.irq.sdadc+0x2` `sdadc_isr` | 读 `0x1c8`，写 `0x1c4` 清标志并调用通知 | 恢复了状态位和事件值对应关系，不能直接把事件命名为半满/全满 |

ISR 对应关系：状态位 `0x2 → event 1`、`0x1 → event 2`、`0x8 → event 3`、
`0x4 → event 4`。前两者构成 `if/else if`，后两者同样如此；同时置位时有优先级，
不能改写成四个互相独立的判断。每次状态读取及回调的先后顺序均保留在恢复代码中。

采样率映射：

| Hz | 48000 | 44100 | 38000 | 32000 | 24000 | 22050 | 16000 | 12000 | 11025 | 8000 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 索引 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
| `.rodata` 分频表值 | 0 | 0 | 0 | 1 | 2 | 2 | 3 | 4 | 4 | 5 |

38000 是实际比较常量 `0x9470`，并非笔误。时钟切换还操作 `0x140`、`0x390`、
`0x39c`、`0x3a8`；该函数不只是把采样率存进配置。

现有 `bare/app/mic_read.c` 忽略初始化返回值，也未为 `sdadc_start` 传入缓冲区。
因此现有链接 smoke test 不能证明录音链路可用。修复应把 channel 映射、缓冲区生命周期、
DMA 计数单位、中断注册和回调解释一起落实；本轮只添加分析材料，未改动运行中的驱动 API。

## 恢复代码的边界

`sdadc_recovered.c` 覆盖配置、采样率切换、启动、停止和 ISR。
它仍调用 `sdadc_analog_mic_start/exit`、`sdadc_analog_aux_start/exit`、
`hal_printf`、`hal_mdelay`；因此不是一个已经完全摆脱厂商库的 ADC 驱动。
模拟 MIC 函数在 `audio.o`，AUX 函数当前由 `vendor_shim.c` 提供。

`_cfg` 在符号表中为 12 字节，本对象只读写前 6 字节，其余字段保持未知。
DMA 参数恢复为 `void *` 是基于寄存器写入和上下文的类型推断；传输单位、双通道缓冲布局、
合法存储区和事件的物理意义还需手册/模拟层/硬件交叉验证。
代码中的地址采用裸数值，避免给尚未核实的寄存器附加错误名称。

## REA 的适用性和当前环境

已阅读上一级 `../rea/AGENTS.md`、REA 原生分析文档及
`../rea/.agents/skills/reverse-engineer-anything/SKILL.md`。
REA 是分析编排和证据工具，原生反编译依赖独立的 Ghidra/Hopper/IDA provider。

本轮观察到：

- 会话没有 REA MCP 工具，PATH 没有 `rea` 命令。
- `../rea` 没有 `dist/`、`node_modules/`，尚不是可直接运行的构建。
- `GHIDRA_INSTALL_DIR` 未设置，PATH 没有 Ghidra 启动器；检查的常见安装位置及
  `/opt`、Documents、Downloads 中没有找到 `analyzeHeadless`。
  这是有边界的环境检查，不等于证明整个机器绝无 Ghidra。
- 因而本轮没有运行 REA 自动反编译，没有虚构 REA Evidence ID。
  使用系统 ar/readelf/nm、LLVM 和人工反汇编恢复完成可行性验证。

后续配好 REA 与兼容的 Ghidra 后，先解包 `.a`，逐 `.o` 导入，保留 ELF 符号和重定位。
不要把可重定位对象强制当作无符号裸固件来加载。按本地 REA CLI 文档可尝试：

```sh
rea inspect "$PWD/docs/lib-analysis/generated/recovery/sdadc.o" --provider ghidra --json
rea decompile "$PWD/docs/lib-analysis/generated/recovery/sdadc.o" sdadc_start --provider ghidra --json
```

这两条是后续操作示例，本轮未执行；该 ELF/架构在所选 provider 上仍需实际导入验证。
REA 的配置安装与反编译引擎准备是单独的环境任务，本轮没有改动客户端配置。

## 复现与后续顺序

在仓库根目录运行：

```sh
python3 docs/lib-analysis/audit_archives.py
clang --target=riscv32-unknown-elf -march=rv32imac -mabi=ilp32 \
  -ffreestanding -std=gnu11 -Wall -Wextra -Werror -Os \
  -c docs/lib-analysis/recovered/sdadc_recovered.c \
  -o docs/lib-analysis/generated/recovery/sdadc_recovered.o
```

以上两个命令本轮均成功执行。审计脚本只读库，写入本目录的生成材料，
不会执行目标固件；`.o` 为可再生中间产物，已忽略版本跟踪。

建议下一轮先完成 `audio.o + sdadc.o` 的模拟链路与板上采集，再处理 SPI/UART/FM，
随后处理 USB 状态机、描述符和端点。蓝牙库保留 `bb_init`、`bb_run_loop` 等入口，
但大量内部名如 `bb_find_on_influence` 不直接说明语义；需沿调用和数据流逐个命名，
结合 HCI 事件与 RF 时序验证，工作量和验证难度高于上述 HAL 模块。
静态库只包含它自身的代码，ROM 地址别名所指向的 ROM 实现仍需独立证据。
