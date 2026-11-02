# AB32VG1 蓝牙控制器恢复

`bare/lib/libbtctrl.a` 的全部非空成员已恢复为可编译 C 源码：32 个归档成员中，
29 个含函数的对象共 374 个已定义 FUNC 符号，另有 1 个仅含 ROM 绝对符号的对象和 2 个空对象。
恢复后端的完整隔离链接不再输入 `libbtctrl.a`，没有保留原库成员兜底。

这完成的是**归档源码替代**。芯片 ROM 服务、`libhal.a` 中的辅助实现仍在使用；
统一 RTT 的蓝牙固定 RAM、ROM 堆、真实中断/调度适配及广播/连接实机验证仍待完成。
目前 `drv_bt_init()` 的默认平台检查继续返回 false，不能把源码恢复当成无线通信已调通。

原归档有 43,400 字节可执行段，SHA-256 为
`60cda98155de21b3feda330d494d38a79d61db6f279f750dc1b12e1c45c4ac9b`。
函数名的混淆名称保持 ABI 兼容；行为依据机器码、重定位和 SDK 调用恢复，不按名称猜测。

## 全库范围

| 原对象 | 恢复源码（`bare/drivers/src/`） | 范围 |
| --- | --- | --- |
| `hct_msg.o`、`hct_proc.o` | `bt_hct_recovered.c` | HCI 消息编码、事件掩码、描述符查找 |
| `hct.o` | `bt_hct_transport_recovered.c` | 命令分发、ACL 分片、信用、H4 队列和所有命令表 |
| `bb.o`、`nanos.o` | `bt_core_recovered.c`、`bt_nanos_recovered.c` | 生命周期、ISR 派发、事件与 ROM 调度适配 |
| `ext_init.o`、`ext_util.o` | `bt_ext_init_recovered.c`、`bt_ext_util_recovered.c` | 固定 RAM、地址、复位与功能参数 |
| `bb_rf.o`、`complex.o` | `bt_rf_recovered.c`、`bt_complex_recovered.c` | 射频配置、采样、补偿、双分量整数校准运算 |
| `baseband_platform.o`、`weak_symbol.o` | `bt_platform_recovered.c`、`bt_weak_symbol_recovered.c` | 平台钩子、IRQ 包装、原始弱绑定与默认值 |
| `bb_utils.o`、`bb_buf.o`、`bb_debug.o` | 同名 `bt_*_recovered.c` | 查找、缓冲队列、诊断 |
| `bt21.o`、`ble.o` | 同名 `bt_*_recovered.c` | 经典/LE 中断适配 |
| `lm.o`、`lm_proc.o`、`lm_sleep.o`、`ble_sleep.o` | 同名 `bt_*_recovered.c` | 任务描述符、消息处理与睡眠窗口 |
| `mgr.o`、`mgr_proc.o`、`mgr_util.o` | 同名 `bt_*_recovered.c` | 广播、扫描、连接发起、白名单、去重和加密任务队列 |
| `ble_evt.o`、`ble_con.o` | 同名 `bt_*_recovered.c` | 事件调度、连接参数、射频描述符和测试模式 |
| `ll.o`、`ll_proc.o`、`ll_cntl.o`、`ll_util.o` | 同名 `bt_*_recovered.c` | 链路状态机、控制 PDU、连接更新、加密流程和 ACL 所有权 |
| `platform_asm.o` | `bt_rom_binding_recovered.c` | 唯一绝对符号 `nanos_env = 0x104e4` |
| `co_list.o`、`ble_txrx.o` | 无需源码 | 原成员没有可分配段、函数或导出符号 |

完整逐成员清单由 [归档审计脚本](test_bt_archive_recovery.py) 生成，记录在
[覆盖报告](generated/recovery/bt-archive-coverage.json)。检查 Clang/GCC 的全部公开符号、
weak/global 绑定、函数段归属、数据大小和偏移。两个 HCI 对象合成一个 C 文件，其公开数据的原始段偏移和布局保持一致。

原始空函数和默认拒绝行为按机器码保留；没有用空桩替代非空实现。
可执行逻辑是可读 C，`.inc` 中只有以 C 指针表达重定位的常量/分发表，没有嵌入原库机器码。
manager 与链路消息对象的编译器私有跳转表随 C 编译器生成，分支行为由差分测试验证，
不要求其机器指令地址或跳转表字节相同。

HCI 内部消息需要 16 字节前缀，不能把普通应用字节数组传给内部接口。
`bthw_irq_init()` 按 `PICADR + 0x1e0` 修改 RAM 向量槽中的 JAL；
可写常驻区、外层上下文保存及真实中断入口仍由板级适配负责。

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

1. **固定内存布局**：原机器码直接访问 `0x11000` 及其后的蓝牙 RAM，`nanos_env` 固定为 `0x104e4`。原 RTT `link.lds` 从 `0x11000` 预留 10 KiB，将 `.btmem.bthw` 放在最前；裸机的 FLASH `0` / RAM `0x20000000` 布局没有满足这个约定。当前 RTT 启用 FM 时选用的 SRAM 布局也需在蓝牙集成前统一调整。
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

广播数据的 32 字节为长度字节加固定 31 字节数据域。这些入口及对应 manager/链路处理已恢复；GAP/GATT 主机服务及经典蓝牙应用 profile 不在该控制器归档替代范围内。

## 选择恢复实现

在仓库根目录执行：

```sh
make -C bare BT_SUPPORT_BACKEND=recovered
```

输出目录为 `bare/build/recovered-bt-support/`，保留原选项名称以兼容已有命令。
默认仍为 `vendor`；显式 `recovered` 模式已从链接参数中移除 `-lbtctrl`。
可与 SPI/FM 恢复选项组合。无垃圾回收的隔离链接检查整个控制器，
普通示例构建会裁剪未引用函数，不能单独证明全库替代。

## nanos 调度适配源码恢复（2026-11-02）

`bt_nanos_recovered.c` 保留原对象的六个导出名称和回调 ABI：

| 函数 | 原机器码行为 |
| --- | --- |
| `nanos_keep_inside_reach` | 返回 0 |
| `lm_proc_do_on_kiss` | 返回 2 |
| `bb_do_on_ordinary` | 依次向 ROM `0x84390` 注册两个定时回调、向 `0x8435c` 注册事件回调 |
| `nanos_save_to_draft` | 调用 ROM `0x845b8`，取返回值右移 4 位后的低 16 位 |
| `bb_find_to_afternoon` | 参数等于 6 时调用 `bthw_soft_kick`，否则将参数原样传给 ROM `0x84344`；之后始终调用 `nanos_event_set_trigger` |
| `nanos_write_inside_process` | 非空参数时读取偏移 8 的半字写到 `0x80e0`，按 `0x800c` bit 10 状态清 pending 并使能；空参数时清除该使能位 |

三个运行期回调保留 `.com_text.bb.nanos` RAM 代码段。定时结构的其他字段含义尚未确认，
恢复代码只表达已确认的半字访问。四个 ROM 入口保留为明确的函数指针调用，
没有使用空实现或模拟成功代替 ROM，也未声称已经重写调度器。

新增原机器码与恢复 C 的 RV32 差分共 144 组、每组 10 次调用，覆盖返回值、
时间截断边界、事件 6 与其他事件、回调注册顺序、空/非空定时参数、重复启停、
使能位初值和两次控制寄存器读取之间的状态变化。另验证导出绑定与 RAM 段归属。
结果见 [差分记录](generated/recovery/bt-nanos-differential.json) 和
[原始反汇编](generated/recovery/bt-nanos.llvm.txt)。这些检查模拟 ROM 返回与寄存器，
不证明 ROM 内部行为、真实中断时序或蓝牙射频工作。

## bb 控制器生命周期源码恢复（2026-11-02）

新增 [bt_core_recovered.c](../../bare/drivers/src/bt_core_recovered.c) 和
[内部 ABI 头文件](../../bare/drivers/inc/bt_core_recovered.h)，覆盖 `bb.o` 的全部 **19 个导出函数**。

| 范围 | 入口 |
| --- | --- |
| 内存与生命周期 | `bthw_get_heap_info`、`bb_init`、`bb_reset`、`bb_off` |
| 中断与调度 | `bthw_soft_isr`、`bthw_isr_do`、`bb_run_loop` |
| 睡眠与事件 | `bb_sleep`、`bb_give_on_enthusiasm`、`bb_is_sleep`、`bb_get_at_clue`、`bb_keep_within_opening`、`bb_fetch_in_event`、`bb_get_without_card` |
| 其他原始导出 | `bb_go_about_way`、`bb_keep_within_error`、`bb_make_on_husband`、`bb_find_on_influence`、`bb_pass_under_iron` |

`bb_find_on_influence` 在原对象中本身就是空返回；`bb_make_on_husband` 返回 1；
`bb_pass_under_iron` 保留原有六条 NOP 和独立调用。没有用这些空操作替代其他未恢复函数。
运行期代码的 `.com_text.*`、`.sleep_text.bb`、`.text.update` 段归属及相关对齐均保留。
C 编译后的指令数会改变，保留 NOP 和寄存器顺序不等于已经验证真实睡眠/唤醒时序。

数据布局也参与对照：

| 段 | 大小 | 约束 |
| --- | ---: | --- |
| `.btmem.bthw` | 1666 字节 | 保留全局 `bb_get_without_vehicle`，4 字节对齐 |
| `.btmem.bb.buf` | 4384 字节 | 指针元数据偏移 0、大小元数据偏移 12、两个缓冲区偏移 20 和 164 |
| `.btmem.bb.sniff` | 52 字节 | 偏移 0 为 16 位标志、2/3 为字节状态、4 起为 48 字节上下文 |
| `.sbss` | 4 字节 | 保留睡眠累计值与 700 边界行为 |

`bb_sleep` 实际返回低 8 位结果，原封装中的 `void` 声明已纠正为 `uint8_t`；
现有调用方仍可以忽略返回值。对恢复源码不额外添加成功判断、超时或地址迁移，
否则就无法先建立与原对象的行为等价基线。

**差分验证：494 组，每组同时执行原对象、Clang 和项目 GCC 编译出的恢复代码。**
覆盖全部导出函数、初始化/复位/关闭/再初始化、全 256 个输出字节、事件标志、
中断状态在回调期间变化、睡眠拒绝路径、时钟选择分支、时间回绕、返回值截断、
700 累计边界、寄存器等待及无完成状态。对比调用参数/顺序、MMIO 轨迹、返回值、
存储区内容、堆元数据输出与栈/被调用者保存寄存器。另验证对象导出、段大小、初值和对齐。
结果见 [差分记录](generated/recovery/bt-core-differential.json) 和
[原始反汇编](generated/recovery/bt-core.llvm.txt)。

无完成状态测试在模拟器观察到持续等待后主动停止；恢复代码保留原有无期限等待，
没有把硬件不就绪伪装成成功。ROM 调用、RF、链路层辅助调用在差分测试中由 hook 建模，
因此测试不证明这些依赖本身或真实广播/连接已可用。

### 固定地址的进一步结论

`bb_init` 和 `bb_reset` 中的 `0x11000` 现已成为可读源码常量，但其用途仍包括
传给 ROM `0x845c8` 和写寄存器 `0x350`。现已恢复的 `ext_init.o` 也有独立的绝对地址访问，
`bb_rf.o` 现也确认直接访问 `0x111a4 + 88 * index` 和 `0x11020..0x1106f`。
因此只修改新源码中的一个常量不能完成搬移，须继续确认 ROM/硬件契约。
当前没有据此修改综合 RTT 的内存布局，也未向开发板烧写含蓝牙模块的新版本。

## ext_init 固定 RAM 与复位源码恢复（2026-11-02）

[bt_ext_init_recovered.c](../../bare/drivers/src/bt_ext_init_recovered.c) 覆盖该对象全部五个导出函数，
对应的 [ABI 头文件](../../bare/drivers/inc/bt_ext_init_recovered.h) 保留原符号名。

| 函数 | 已恢复行为 |
| --- | --- |
| `bb_write_without_passenger` | 清 `0x8000` bit 8，依次置位并等待 bit 31、30 自清零；向 `0x11000..0x1101f` 按半字写入 16 个 `0x6000`，再清使能/确认 pending |
| `bt21_set_on_tennis` | 调用五个 ROM 初始化入口，清 36 字节上下文，设置三个固定 RAM 指针，反转六字节设备地址，初始化固定区、常量和基带寄存器 |
| `ext_init_reserve_to_word` | 重新设置设备地址与寄存器，向 `0x8020` 写入时钟参数和 bit 31，等待完成，再读取 ROM 时钟并打印 |
| `ext_init_pick_of_edge` | 保存 `0x544` 的 bits 7/8，屏蔽后读取时钟，复位并调用辅助初始化，再恢复保存的时钟和中断位 |
| `ext_init_pass_within_equivalent` | 打印参数后，读取上下文中的指针；按参数是否非零设置/清除其偏移 12 字节的 bit 4 |

保留 `.btmem.bb` 的 36 字节全局上下文、`.btmem.bb.rf` 的单字节状态和
`.rodata.bb.tbl` 的 24 字节常量表，以及各自的初值和对齐。上下文仍按已证实的偏移表示，
没有凭混淆符号名给未知字段赋予含义。设备地址辅助函数仍由 `ext_util.o` 提供；
其机器码将传入六字节复制到上下文偏移 24，不保留临时缓冲区指针。

本次确认的绝对地址包括 `0x11000` 的半字表、上下文中的 `0x11190/0x111e8/0x11240`，
以及传给 ROM `0x845ac` 的 `0x11644/0x11658`。其他 ROM 入口为
`0x845a8/0x845c0/0x845c4/0x845cc/0x845b8`。这些调用均显式保留，尚未恢复 ROM 内部实现。

**差分验证：729 组，每组执行原始对象和 Clang/GCC 两版恢复代码。**
覆盖反复初始化/复位、六字节地址逐位转换、全 256 个原始标志字节、非零参数高位、
时钟边界和逐位输入、等待次数、三处等待不完成、中断寄存器在读取或外部调用期间变化，
以及打印回调修改上下文指针后的访问顺序。比较 MMIO 与固定 RAM 写入顺序/宽度、
调用参数和当时内存、最终数据、栈与保存寄存器，另检查导出和数据段布局。
测试模型对 `ext_util.o` 的六字节复制建模；其余外部/ROM 调用仍使用受控 hook。
无完成状态由模拟器在持续轮询后停止，源码保留原对象的无期限等待。

结果见 [差分记录](generated/recovery/bt-ext-init-differential.json) 和
[原始反汇编](generated/recovery/bt-ext-init.llvm.txt)。恢复后端通过 GCC 构建及独立链接检查，
不再抽取原 `ext_init.o`；这些结果不代表 ROM、射频或实际复位时序已通过硬件验证。

## bb_rf 射频配置与校准流程源码恢复（2026-11-02）

[bt_rf_recovered.c](../../bare/drivers/src/bt_rf_recovered.c) 和
[ABI 头文件](../../bare/drivers/inc/bt_rf_recovered.h) 覆盖原对象的 **28 个导出函数和 9 个内部函数**。

| 范围 | 恢复内容 |
| --- | --- |
| 配置与增益 | 7 组配置表、平台配置选择、字段输出、增益上下限、四档寄存器值和 RSSI 数值换算 |
| 射频寄存器接口 | `0x81b4/0x81b8/0x8070` 访问序列、完成位等待、信道设置与完成确认 |
| 射频开关 | 保留各位独立读改写、先后顺序、原有无位变化的读写以及 NOP 延时调用 |
| 校准主流程 | 基准/激励采样、9 档双分量采样、外部计算、正负补偿字节编码及 9 个寄存器写回 |
| 睡眠/恢复 | 保存 11 个寄存器（44 字节）、关闭射频、唤醒重置/延时、恢复寄存器和中断位 |
| 固定 RAM | `0x111a4 + 88 * index` 的两位计数；`0x11020..0x1106f` 的 40 个偶数和 40 个奇数表项 |

保留全部原始函数的段归属，包括 `.sleep_text.bb`、`.sleep_text.bb.rf`、
`.com_text.rf`、`.com_text.bb.rf.sleep`、`.com_text.bb.rf.sleep.rssi`、
`.com_text.isr.txrx` 和增益函数段。三个弱入口 `bb_rf_send_by_burn`、
`bb_rf_show_by_line`、`bb_rf_sell_by_patience` 仍允许板级覆盖。
原本为空的 `bb_rf_send_by_burn`/`bb_rf_go_inside_log` 以及返回零的
`bb_rf_make_of_position` 按机器码保留，不用它们代替其他未恢复操作。

常量数据保持 89 字节及 4 字节对齐，`.sdata` 保留 4 字节配置指针，初始指向第五组配置。
`bb_show_with_stay` 实际写入 44 字节，不把上层 48 字节上下文的最后四字节误当成寄存器。
RSSI 换算函数的恢复只证明数值行为，不代表已经完成接收强度标定。

校准读取 `0x7da4` 后按 bit 20 扩展符号，再做有符号除以 128；没有用右移替代负数除法。
`bb_rf_fetch_without_dish`、`bb_rf_make_in_job`、`bb_rf_make_for_branch` 的校准运算已由 `bt_complex_recovered.c` 替代。
两个 32 位分量通过 RV32 的 a0/a1 传递和返回，保留原 ABI。另有 31 组 RF 与真实校准运算联合差分，
同时执行原始 RF/complex 与恢复 RF/complex，不使用校准结果桩。

**差分验证：1,472 组，每组执行原对象及 Clang/GCC 两版恢复代码。**
覆盖全部导出和内部函数，比较有序 MMIO、固定 RAM 写入宽度/值、配置指针、外部调用参数、
返回值、上下文、栈和保存寄存器；另检查函数绑定、段归属、表内容/对齐及指针初值。
测试包含全部 256 个配置输入及计数字节、开关组合、增益与等待次数边界、采样符号边界、
补偿截断、弱入口覆盖、中断状态变化和三类硬件等待不完成。NOP 延时循环实际执行，
无完成状态由模拟器停止观察，源码保留原有等待。

结果见 [差分记录](generated/recovery/bt-rf-differential.json) 和
[原始反汇编](generated/recovery/bt-rf.llvm.txt)。外部校准计算使用受控 hook，
包括注入正负边界返回值来检查调用方编码；未执行真实模拟射频，也未验证芯片上的延时长度。
恢复后端 GCC 构建通过；无垃圾回收的独立 RV32 链接确认排除了原 `bb_rf.o`，
完整隔离链接进一步确认不再输入 `libbtctrl.a`。

## 验证与复现

需要 Clang、项目 RISC-V GCC、RISC-V LLD、Unicorn 2.1.4 和 pyelftools。
集成检查另需主机 `cc`、`make`、`readelf`。可使用统一入口：

```sh
python3 docs/lib-analysis/test_bt_all.py --gcc /path/to/riscv64-unknown-elf-gcc --linker /path/to/ld.lld
make -C bare BT_SUPPORT_BACKEND=recovered -j4
```

独立检查：

```sh
python3 docs/lib-analysis/test_bt_archive_recovery.py --gcc /path/to/riscv64-unknown-elf-gcc
python3 docs/lib-analysis/test_bt_support_recovery.py --gcc /path/to/riscv64-unknown-elf-gcc --linker /path/to/ld.lld
python3 docs/lib-analysis/test_bt_hct_transport.py --recovered --gcc /path/to/riscv64-unknown-elf-gcc --linker /path/to/ld.lld
python3 docs/lib-analysis/test_bt_integration.py --linker /path/to/ld.lld
```

当前 30 份差分报告共 **26,529 组通过**，包含 HCI 的全部 65,536 个内部 descriptor ID 查找、
双编译器控制器模块比较及 31 组 RF/真实校准运算联合检查。Clang/GCC 的完整隔离链接、
裸机恢复后端构建和当前综合 RTT 默认配置构建均通过。RTT 构建不含蓝牙启用验证。

差分报告位于 `generated/recovery/bt-*-differential.json`，每个模块列出输入分组和源码/对象哈希。
检查原始 RV32 与恢复 C 编译产物的返回值、调用顺序/参数、寄存器读写、固定 RAM、上下文和消息内容；
保留原始符号、数据布局和 weak 绑定。共享测试覆盖每个原始函数，包括静态消息回调。

测试中的 ROM、分配器、外部任务/射频接口由受控 hook 建模，定制 restore 指令使用 ABI shim。
它们不是对所有输入的形式证明，也不证明真实 RF、ROM 内部、CPU IRQ 入口和时间精度。
原有无期限硬件等待保留，由模拟器在无完成状态的测试中停止观察。
隔离链接采用人工地址布局，不是可烧录固件。

## 源码替代之后的工作

`libbtctrl.a` 已没有待恢复的非空 `.o`。综合 RTT 已完成固定 RAM、ROM 堆及 IRQ/调度适配，
并烧录验证 HCI、BLE 广播发送和扫描接收。接下来是连接与 ACL、NimBLE 主机栈及 GATT，
同时继续优化综合固件的内存预算。
这与继续逆向芯片 ROM 或 `libhal.a` 中其他成员是不同范围。

## 综合 RTT 实板验证（2026-11-02）

使用 CH340 `/dev/ttyUSB0`，DCF 经 loader CRC 验证后提交。
`BSP_USING_BTCTRL` 直接编译共享恢复 C 文件及 `drv_bluetooth.c`，没有链接 `libbtctrl.a`。
固定区 `0x11000..0x137ff` 当前使用 6540 字节，`bb_get_without_vehicle == 0x11000`。
`link_bt_ram.lds` 将其余代码/数据和内核分别放入两块 SRAM；FM、音频、LED 与串口仍在同一工程。

板级适配参考手册配套 `test_program/ab32v110-ble-test.zip` 中的
`packages/bluetrum_sdk-latest/bluetrum_nimble/porting/npl/rtthread/src/ble_platform.c`：
先冷复位控制器再初始化，分别保留硬中断待处理标志和软事件唤醒标志。
RTT 的优先级 5 工作线程独占控制器调用；UART shell 仅提交测试请求。
该压缩包也含 NimBLE 源码，当前尚未解包接入。恢复代码继续保留原始行为，兼容处理位于 RTT 端。

HCI 实测完成事件：

```text
Reset:               04 0e 04 05 03 0c 00
Read Local Version:  04 0e 0c 05 01 10 00 09 03 00 09 42 06 03 00
Read BD_ADDR:        04 0e 0a 05 09 10 00 02 00 ab 32 00 01
LE Read Buffer Size: 04 0e 07 05 02 20 00 1b 00 05
```

BLE 缓冲区报告为 27 字节、5 个包；这仅证明查询结果，不等于 ACL 数据传输已验证。
广播参数/数据/启用、扫描参数/启用均收到成功状态；广播还由独立电脑 BlueZ 实际接收，
名称 `AB32-RTT-TEST`、静态随机地址 `C2:00:AB:32:00:01`。
板端被动扫描收到广播报告，并能停止、重启。它目前是不可连接测试广播，无 GATT 服务。

### 空闲时隙被误判为经典蓝牙占用

最初 HCI 成功但无实际收发，只出现 `LE pending=0x80` 定时事件。
只读检查芯片 ROM 表明 `0x84664` 跳转到 `0x8aa3e` 调度器，
其提交路径 `0x8a86a` 调用 `0x10420` 保存的冲突回调；默认回调为 `0x8a7fc`。
该回调把经典槽低 11 位与保留描述符偏移低 11 位比较。
本板 BLE-only 初始化下，槽值 `0x6000`、读到的描述符为零，结果每次都是 busy；
对照测试记录为 **22 次检查、22 次 busy**，广播未被电脑发现。

ROM `0x845c0`/`0x845c4` 分别写入 `0x1042c`/`0x1042e` 两个经典上下文计数。
RTT 增加 BLE-only 策略：两者均为零时不阻挡 BLE，有经典上下文时保留原回调判断。
通过 `0x84620` 的七项回调注册 ABI 安装，保留其余六项，在冷初始化和 HCI Reset 时重新安装；
不修改 ROM 或原恢复函数。启用该策略后出现 `LE pending=0x08` 完成事件，电脑收到广播，
板端扫描在一次约 5 秒观察中累计收到 **886 条广播报告**。
这些结果支持空闲冲突判断是本次收发故障的原因；不证明经典蓝牙共存已实现。

当前测试命令、内存限制和 FM 互斥行为见 [RTT 使用说明](../../rtt/README.md)。
最终镜像的 SHA-256、内存和验证结果见 [硬件报告](generated/recovery/bt-rtt-hardware.json)，
串口原始输出见 [验证日志](generated/recovery/bt-rtt-hardware.log)。最终回归的扫描计数为 534、1578，
广播/扫描重复启停前后堆可用量均为 4056 字节；FM 在蓝牙占用期间启动返回 `-RT_EBUSY`。
广播/扫描只验收了控制器的基础收发；连接稳定性、ACL 流控、加密、NimBLE、GATT、
长时间运行和 FM/蓝牙同时工作仍需分别验证。
