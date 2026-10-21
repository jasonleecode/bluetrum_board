# 蓝牙平台与 nanos 层恢复记录

对象：`bare/lib/libbtctrl.a:baseband_platform.o`、`nanos.o`。本记录依据原始 RV32 指令、重定位、SDK 寄存器头文件与 RT-Thread 平台源码；没有硬件验证。

## 可替换范围

`bare/drivers/src/bt_platform_recovered.c` 强定义三个原来为 weak 的函数，并保留一个八字节 weak 状态对象。它是**部分平台恢复**，不会替代整个 `baseband_platform.o`；其他库成员引用原对象中的弱钩子时，原对象仍可被链接，三个 strong 实现优先。

| 函数 | 已确认行为与 ABI |
| --- | --- |
| `void baseband_platform_pass_by_hello(void)` | 对 `PLL1DIV`（0x3a4）依次执行四次独立 volatile RMW：AND 0xff000fff、OR 0x00eba000、AND 0xfffff7ff、AND ~0x1c。不能合并成一次写入。 |
| `void bthw_irq_init(void)` | 状态字节 0 清零，字节 1 接收 `bt_get_packet(255)` 返回值低八位；调用 PLL 配置；注册 IRQ 1 的普通回调；有条件地修补高优先级向量槽并启用 bit 22。 |
| `void bthw_isr_do_plus(void)` | 调用 `bthw_isr_do()` 后，对未公开命名的 0x3f8 寄存器 OR 3；普通函数返回。 |

本地 IRQ 1 回调（原符号 `baseband_platform_get_of_officer`）按顺序执行：`os_interrupt_enter()` → 0x3f8 AND ~3 → `bthw_thread_post()` → `os_interrupt_leave()`。其普通调用 ABI 由原始 save/restore/ret 序列和 SDK 调度器共同确认；没有 CSR 指令或 `mret`。它只屏蔽寄存器的低两位并通知工作线程，后续由 `bthw_isr_do_plus` 处理并恢复这两位。尚不能给这两位分配更具体的硬件含义。

`bb_rf_read_inside_upper` 原符号大小为 8，恢复定义保持大小、弱绑定、`.sbss` section 和四字节对齐。这里只确认字节 0、1 的初始化操作，剩余字段不命名；不能将整个对象缩减为两个字节。

`bt_get_packet` 的本调用点传入 32 位寄存器参数 255、只消费返回值低八位；恢复声明使用 `uint32_t` 参数和返回值描述这个调用约定，不声称找回原始 C typedef。其他原对象中的弱空钩子没有足够证据恢复原始参数类型，故不凭空补声明或实现。

## 向量写入与中断 ABI

SDK `rtt/bsp/libraries/hal_libraries/bmsis/include/ab32vg1.h` 确认：0x544 是 `PICEN`，0x548 是 `PICPR`，0x54c 是 `PICADR`。

`bthw_irq_init` 读取 `PICADR`，目标槽为 `PICADR + 0x1e0`。令 `delta = (uint32_t)bb_rf_take_without_start - slot`，只有无符号 `delta < 0x100000` 时才写入以下指令字，并依次设置 `PICPR`、`PICEN` 的 bit 22：

```c
jal = ((delta << 20) & 0x7fe00000)
    | (delta & 0x000ff000)
    | ((delta << 9) & 0x00100000)
    | 0xef; /* jal ra, target */
```

原实现不允许负位移，也不检查对齐，恢复实现按原行为保留。差分测试覆盖临界值与奇数位移；这表示行为一致，不表示这些地址适合真实上板。

`rtt/bsp/libraries/hal_libraries/bmsis/source/startup.S` 在 `.vector + 0x1e0` 留有跳转、`nop`、`mret`。库只替换第一条指令；被调用函数按普通 `ret` 返回后，由向量包装执行 `mret`。不能把这些普通回调改成 `interrupt("machine")`，也不能只把本 C 文件接到一个普通函数返回的 trap 入口就认为中断链路成立。CPU 保存现场、常驻内存、可写向量区、缓存一致性和优先级均需要真实平台提供。

## bare 平台缺口

逆向开始时，`bare/drivers/src/vendor_shim.c` 的 `register_isr` 只把函数指针存到数组，`interrupt_handler_c` 是空函数，`bthw_thread_post`、`nanos_event_set_trigger`、`bthw_soft_kick` 也都是空函数。这些占位函数足以满足链接，不能驱动蓝牙事件处理。`hal_get_ticks` 以调用次数自增，也不能作为真实的协议定时来源。

对照的现成实现位于：

- `rtt/bsp/libcpu/cpu/interrupt.c`：`rt_hw_interrupt_install` 在保护下更新向量表与 `irq_mask`，配置 `PICPR/PICEN`；`cpu_irq_comm_do` 读取 `PICPND & irq_mask` 后分发。
- `rtt/bsp/board/board.c`：`register_isr` 对接上述安装函数；OS 中断 enter/leave、真实 RTOS tick 与延时。
- `rtt/bsp/libraries/hal_libraries/bmsis/source/startup.S`：复制常驻代码、写入 `PICADR`、建立芯片的向量包装。

当前 bare 启动文件没有相应芯片向量包装，链接脚本也没有照搬 SDK 的常驻区布局。恢复代码不替这些缺口做虚假兜底。上层封装可以记录 pending 工作，但记录 pending 不等于真实 IRQ 已安装，也不等于 nanos 的 ROM 调度链路已成立。

## nanos.o：已分析，暂不替换

| 原符号 | 原机器码行为 |
| --- | --- |
| `nanos_keep_inside_reach` | 返回 0；原参数与高层语义不明。 |
| `lm_proc_do_on_kiss` | 返回 2；原参数与高层语义不明。 |
| `bb_do_on_ordinary` | 调用固定 ROM 地址 0x84390，a0/a1 分别为 `nanos_save_to_draft` / `nanos_write_inside_process`；随后调用 0x8435c，a0 为 `bb_find_to_afternoon`。从参数形态推测是回调注册，ROM 内部尚未验证。 |
| `nanos_save_to_draft` | 调用 ROM 0x845b8，返回 `(result >> 4) & 0xffff`。时间源/单位尚未确认。 |
| `bb_find_to_afternoon` | a0 等于 6 时调用 `bthw_soft_kick`，其他情况调用 ROM 0x84344，随后均调用 `nanos_event_set_trigger`。 |
| `nanos_write_inside_process` | a0 非空时取结构偏移 +8 的 uint16 写入 0x80e0；若 0x800c bit 10 未置位，则向 0x8018 写 bit 10 后，在第二次读取的 0x800c 上 OR bit 10。a0 为空时清除 0x800c bit 10。 |

这些 ROM 地址、结构字段和 baseband 寄存器语义仍依赖指定芯片 ROM。另一对象 `bb.o` 中 `bthw_soft_isr` 也只是尾跳到 ROM 0x84664。因此当前不能声称已有独立开源 scheduler、完整软中断流程或完全摆脱 ROM 的控制器。

## 验证

`docs/lib-analysis/test_bt_platform_recovery.py` 将原 `baseband_platform.o` 与恢复 C 分别链接成 RV32 ELF，在相同 Unicorn 模型下比较调用顺序、全部 MMIO 读写、状态对象和向量指令字。**62 例通过**，含位移上下界、负位移、奇数位移、返回值低字节截断、初始状态保留、重复初始化、IRQ/工作处理顺序及外部调用改变控制寄存器的情况。

```sh
PYTHONPATH=/tmp/ab32-recovery-deps python3 docs/lib-analysis/test_bt_platform_recovery.py \
  --linker /path/to/ld.lld
```

`bt-platform-differential.json` 保存对象和源文件 SHA-256、测试模型边界和结果；`bt-platform.llvm.txt` 保存原对象的 LLVM 反汇编。依赖调用使用 hook，vendor restore_0/1 的自定义指令通过 ABI shim 模拟。向量指令字只比较写入值，没有执行真实 IRQ、ROM、射频或硬件时序；这些测试不能证明板上蓝牙可通信。
