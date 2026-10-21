# AB32VG1 蓝牙控制器恢复

本轮围绕 `bare/lib/libbtctrl.a` 的 HCI 消息通路、初始化和中断适配，完成两个对象的 C 替换及三个平台接口的部分恢复，修正裸机封装中的数据和中断处理问题。射频、链路层、调度 ROM 和主机协议栈仍有依赖，**目前不能作为可直接广播、连接的完整蓝牙固件**。

原库包含 32 个对象、374 个已定义 FUNC 符号和 43,400 字节可执行段；其中一些函数名经过混淆。分析依据机器码、重定位和 SDK 调用，不依据混淆名称的英文含义。归档 SHA-256 为 `60cda98155de21b3feda330d494d38a79d61db6f279f750dc1b12e1c45c4ac9b`。

## 已恢复的范围

| 范围 | 实现 | 当前边界 |
| --- | --- | --- |
| `hct_msg.o`、`hct_proc.o` | [bt_hct_recovered.c](../../bare/drivers/src/bt_hct_recovered.c) | 完整替换两个对象：8 个导出函数、2 个表回调和 3 个数据对象；仍调用 vendor HCI 状态、描述符和 ROM |
| `baseband_platform.o` | [bt_platform_recovered.c](../../bare/drivers/src/bt_platform_recovered.c) | 恢复 3 个有效导出、IRQ 1 本地回调及 8 字节弱状态；其余弱钩子仍由原库提供 |
| 应用封装 | [drv_bluetooth.c](../../bare/drivers/src/drv_bluetooth.c)、[drv_bluetooth.h](../../bare/drivers/inc/drv_bluetooth.h) | 平台就绪检查、H4 接收确认、延后处理和发送边界防护 |
| 库接口 | [btctrl_hal.h](../../bare/drivers/inc/btctrl_hal.h) | 当前封装所用控制器 ABI；不是 GAP/GATT API |

HCI 消息实现覆盖消息头偏移、Command Complete/Status 格式、信用字节更新、事件 mask、内部描述符查找和队列派发。它保留原库的符号及表布局；内部消息指针需要 16 字节前缀，不能把普通应用字节数组传给这些内部接口。

平台恢复包含 `baseband_platform_pass_by_hello()`、`bthw_irq_init()`、`bthw_isr_do_plus()`。其中 `bthw_irq_init` 还会按 `PICADR + 0x1e0` 修改 RAM 向量槽中的 JAL 指令；正确的向量包装、可写常驻区与外层上下文保存仍由板级实现负责。详见 [平台恢复记录](generated/recovery/bt-platform-notes.md)。

## 修复的封装问题

### HCI 接收与缓冲生命周期

`hci_host_recv_packet(buf, len)` 得到的是包含 H4 类型字节的完整包：事件 `0x04`，ACL `0x02`。指针属于控制器内部队列，应用必须在回调返回之前复制需要保留的数据。

原封装没有用户回调时直接返回，遗漏 `hct_tx_done()`。原始机器码执行证明确认遗漏会让后续发送停住。现在无消费者或包被丢弃时也发出完成通知；只有类型、头部、声明长度以及 `uint16_t` 长度范围都有效的包才交给应用。

`hct_tx_done()` 自身只安排事件 3，随后调度回调才释放队首消息。不能把它理解成同步释放，也不能在接收回调里重入控制器 API。

### 命令与 ACL 发送

| 发现 | 原库行为 | 封装修复或约束 |
| --- | --- | --- |
| 命令 OCF 高位被忽略 | `0x2006`、`0x2106`、`0x2206`、`0x2306` 进入相同描述符 | 拒绝 OCF 的 bits 8..9 非零，避免别名路由 |
| 命令信用不足 | 通常信用为 0 时丢弃命令；有参数的路径可能先复制再释放，`0x0c35` 有例外 | `drv_bt_hci_cmd()` 的 `true` 只代表通过封装并调用库；主机仍需按 Command Complete/Status 管理信用 |
| ACL 只处理单个分片 | 按路径复制 `min(len, 27)` 或 `min(len, 679)`，超长数据静默截断 | 按相同路径容量拒绝超长输入，不假装完成自动分片 |
| ACL 缓冲耗尽 | 特定路径返回 `true`，但只产生耗尽事件，没有数据入队 | 不能把返回值当成可靠的排队或发送成功；仍需主机流控 |
| handle/flags 高位 | handle 被截为 12 位，flags 原样写入内部字段 | 拒绝 `handle > 0xfff`、`flags > 0xf`，避免隐式截断 |

容量判定是 `flags <= 3 && bb_utils_keep_of_age(handle) == 4` 时为 27 字节，否则为 679 字节；因此不能对所有路径简单套用 27。RX 机器码表明 flags 对应 ACL 头 bits 12..15，封装按原始 PB/BC 四位字段传递。连接是否有效及实际协议分片语义仍由控制器/主机处理。

这里没有增加隐藏发送队列或将丢失的状态伪造成成功反馈。用户需使用真正的 HCI 主机状态机处理信用、完成事件、L2CAP 分片及上层协议。

### 中断与轮询

IRQ 1 顶半部的流程是：进入中断记账 → `0x3f8 &= ~3` → `bthw_thread_post()` → 离开中断记账。原裸机 `bthw_thread_post` 为空，`drv_bt_isr` 又只调用 `bthw_isr_do`，遗漏后续恢复 `0x3f8` 低两位的操作。

现在默认 `bthw_thread_post()` 调用 `drv_bt_isr()` 标记待处理；`drv_bt_run_loop()` 在主循环中调用 `bthw_isr_do_plus()` 处理并恢复这两位，然后调用软处理及调度入口。`drv_bt_isr()` 不再直接执行控制器工作，调用方需持续轮询。

该组合是基于已恢复 IRQ 流程的裸机适配，不是对完整 SDK 线程调度策略的等价证明。芯片中断入口、时序及 ROM 工作处理没有在此次离线测试中执行。

## 初始化为什么现在默认失败

原 `drv_bt_init()` 调用 `bthw_irq_init()`、`bb_init()` 后无条件返回成功。但当前裸机工程缺少必须的平台条件：

1. **固定内存布局**：原机器码直接访问 `0x11000` 及其后的蓝牙 RAM，`nanos_env` 固定为 `0x104e4`。RTT 链接脚本从 `0x11000` 预留 10 KiB，将 `.btmem.bthw` 放在最前；裸机的 FLASH `0` / RAM `0x20000000` 布局没有满足这个约定。
2. **控制器堆初始化**：SDK [blehr_app.c](../../rtt/bsp/app/blehr_app.c) 先调用 `bthw_get_heap_info()`，再调用 ROM `0x84140` 的 `nsmem_cb_init()`。输出包括块指针元数据、16 位大小元数据和块数 3，不是普通 malloc 缓冲区。`bb_init()` 没有代做这一步。
3. **启动、时钟与中断**：当前裸机启动和 shim 缺少真实芯片向量包装、ISR 分发和完整唤醒实现；仅存 ISR 指针不能处理硬件中断。
4. **ROM 调度依赖**：`bb_run_loop()` 仍进入 ROM `0x84350`，`bthw_soft_isr()` 进入 `0x84664`，还有其他 ROM 队列、定时和分配入口。

为避免占位代码继续报成功，新增可由板级实现覆盖的 `bool drv_bt_platform_init(void)`，默认弱实现返回 `false`；此时 `drv_bt_init()` 在任何控制器硬件调用前返回失败。只有板级实现完成上述前提，才可以返回 `true`。不能只把默认返回值改成 true；本轮没有盲目改写整个启动文件或链接脚本。

相关 IRQ 注册、分发和唤醒 shim 已设为 weak，便于板级实现提供真实处理。重复初始化也会返回失败；成功仅表示初始化调用返回，并不代表射频通信已成立。

完整调用链、对象偏移、固定地址及 RF 恢复边界见 [初始化审计](generated/recovery/bt-init-audit.md) 和 [机器可读证据](generated/recovery/bt-init-audit.json)。

## 广播、扫描、连接入口

已从 `hct.o` 命令表和 `mgr_proc.o` 的回调重定位追到以下入口。它们经过内部消息及 ROM 派发，并非 HCI 函数直接调用 manager：

| HCI opcode | 操作 | 参数字节数 | 内部消息 ID | manager handler |
| --- | --- | ---: | --- | --- |
| `0x2005` | 设置随机地址 | 6 | `0x040d` | `mgr_proc_set_inside_cash` |
| `0x2006` | 设置广播参数 | 15 | `0x040e` | `mgr_proc_show_under_diamond` |
| `0x2008` | 设置广播数据 | 32 | `0x0410` | `mgr_proc_give_by_serve` |
| `0x2009` | 设置扫描响应数据 | 32 | `0x0411` | `mgr_proc_give_for_clothes` |
| `0x200a` | 广播开关 | 1 | `0x0412` | `mgr_proc_fetch_into_dream` |
| `0x200b` | 设置扫描参数 | 7 | `0x0413` | `mgr_proc_go_without_purple` |
| `0x200c` | 扫描开关 | 2 | `0x0414` | `mgr_proc_leave_to_result` |
| `0x200d` | 建立连接 | 25 | `0x0415` | `mgr_proc_send_on_muscle` |
| `0x200e` | 取消连接建立 | 0 | `0x0416` | `mgr_proc_tell_within_string` |

广播数据的 32 字节为长度字节加固定 31 字节数据域。定位到入口不等于已经恢复对应的射频状态机；本轮也未实现 GATT 服务、配对或经典蓝牙应用 profile。

## 选择恢复实现

在仓库根目录执行：

```sh
make -C bare BT_SUPPORT_BACKEND=recovered
```

输出目录为 `bare/build/recovered-bt-support/`。默认仍为 `vendor`，可与 `SPI_BACKEND=recovered`、`FM_BACKEND=recovered` 组合，各后端依次增加输出子目录。`SUPPORT` 明确表示只替换部分支持代码；无论哪种后端都需要真实板级初始化。

独立链接检查确认恢复模式不再抽取 `hct_msg.o`、`hct_proc.o`，三处平台入口使用恢复的 strong 定义。`baseband_platform.o` 仍为其他弱钩子被抽取，`hct.o`、`bb.o`、`bb_rf.o` 和 ROM 等依赖仍保留。

## 验证与复现

需要 Clang、RISC-V LLD、Unicorn 2.1.4、pyelftools；集成测试另需主机 `cc`、`make`、`readelf`。从仓库根目录运行，LLD 不在 PATH 时追加 `--linker /path/to/ld.lld`：

```sh
python3 docs/lib-analysis/test_bt_hct_recovery.py
python3 docs/lib-analysis/test_bt_platform_recovery.py
python3 docs/lib-analysis/test_bt_hct_transport.py
python3 docs/lib-analysis/test_bt_integration.py
```

| 验证 | 结果 |
| --- | --- |
| HCI 消息模块差分 | **581 组通过**，其中一组穷举全部 65,536 个 16 位内部 descriptor ID；比较内存结果、调用顺序、非 void 返回值及表布局 |
| 平台模块差分 | **62 组通过**，比较 MMIO、IRQ 通知、状态和向量指令字；另检查弱状态对象大小、section 和四字节对齐 |
| 原始 HCI 传输机器码探测 | **82 组通过**，验证信用丢弃、OCF 别名、ACL 截断/耗尽、H4 flags、借用缓冲与完成通知 |
| 封装及构建集成 | 默认平台拒绝初始化、回调确认、延后 IRQ、发送边界、8 种构建组合和两种独立 RV32 链接通过 |

测试结果：[HCI 差分](generated/recovery/bt-hct-differential.json)、[平台差分](generated/recovery/bt-platform-differential.json)、[HCI 原始行为探测](generated/recovery/bt-hct-transport-probes.json)。原对象反汇编：[HCI 消息](generated/recovery/bt-hct.llvm.txt)、[平台](generated/recovery/bt-platform.llvm.txt)、[HCI 传输](generated/recovery/bt-hct-transport.llvm.txt)。

差分测试执行原始 RV32 机器码及恢复 C 编译后的机器码，但 ROM、分配器、调度器及外部调用使用桩，自定义 restore 指令使用 ABI shim。传输探测只运行原库，不是对整个 `hct.o` 的源码恢复。向量写入只比较指令字，没有模拟真实 CPU IRQ 入口或射频时序。集成链接采用人工布局，不是可烧录固件。

## 继续推进顺序

- [ ] 补齐芯片启动、固定 RAM、ROM 堆、真实中断与调度适配；先验证 HCI Reset 和 Command Complete。
- [ ] 恢复 manager 的广播/扫描命令处理，再沿 RF 配置和状态机逐个做差分。
- [ ] 完成广播与扫描的实机验证，再验证连接、ACL 数据和主机协议栈。

当前阶段保留硬件 RF 后端，没有以软件模拟无线电或假 HCI 响应代替真实蓝牙。
