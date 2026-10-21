# USB 驱动恢复记录

本轮从 `bare/lib/libhal.a` 恢复五个对象的全部 33 个导出函数，操作芯片 USB 控制器寄存器和 DMA，不是软件模拟 USB。原库机器码与恢复代码共通过 1066 项离线差分测试。**这完成了底层传输代码的第一阶段恢复，尚未完成可在 Linux 上枚举的设备固件。**

## 恢复范围

| 原库对象 | C 实现 | 函数数 | 差分用例 |
| --- | --- | ---: | ---: |
| `usb_comm.o`、`usb_detect.o` | [usb_comm_recovered.c](../../bare/drivers/src/usb_comm_recovered.c) | 10 | 185 |
| `usb_device_ep.o` | [usb_ep_recovered.c](../../bare/drivers/src/usb_ep_recovered.c) | 8 | 391 |
| `usb_device_enum.o` | [usb_enum_recovered.c](../../bare/drivers/src/usb_enum_recovered.c) | 10 | 387 |
| `usb_device_hid.o` | [usb_class_recovered.c](../../bare/drivers/src/usb_class_recovered.c) | 5 | 103 |

包括端点选择、初始化、分包收发、停止/清除、EP0 控制传输、延迟地址设置、复位、连接检测以及 HID 定时/传输辅助函数。保留原库弱符号、函数代码段和已观察到的行为，包括无超时等待、非对称 STALL 操作等；这些恢复函数不应被误认为已经修正所有原库缺陷。

`usb_device_run_loop.o` 的设备生命周期与全局上下文仍用原库；`usb_device_audio.o` 和 `usb_device_table.o` 已分析但未替换。详见 [类与描述符审计](generated/recovery/usb-class-audit.md)、[原始描述符与对象哈希](generated/recovery/usb-class-audit.json) 和 [EP0 细节](generated/recovery/usb-enum-notes.md)。

## 修正的接口错误

旧封装的几个声明与机器码 ABI 不一致，会访问错误地址或把初始化成功误当作可通信：

| 入口 | 恢复出的接口/行为 |
| --- | --- |
| `usb_ep_do_transfer` | 参数为端点状态指针和 16 位字节数；旧调用传入数字 `2`，实际会解引用地址 2 |
| `ude_init`、`ude_run_loop_execute`、`uda_init` | 需要各自上下文指针，不能无参数调用 |
| `usb_interrupt_disable` | 需要寄存器基址指针 |
| `usbchk_connect(mode)` | 检测/切换连接状态，不负责安装中断或完成枚举 |
| `usb_ep_start_transfer`、`usb_ep_transfer` | 返回 0 表示待处理，1 表示完成，不是发送字节数 |
| `usbd_ep0_transmit` | 返回 1 只表示已排入当前 EP0 数据处理，不表示主机收到完整传输 |

[usb_ep_recovered.h](../../bare/drivers/inc/usb_ep_recovered.h) 定义 24 字节 RV32 端点状态；地址字段固定为 32 位。`eps[0]` 是当前选择，`eps[1..4]` 是 IN 0..3，`eps[5..8]` 是 OUT 0..3。选择函数返回之前的端点指针，恢复选择时该指针必须有效。

EP0 上下文在 RV32 下为 40 字节，前 24 字节匹配端点状态；状态、地址、配置、模式在偏移 24–27，标志字在 28，SETUP 数据在 32。原库修改标志时使用偏移 31 的字节访问，恢复实现保留这一点。

## 为什么还不能枚举

`ude_run_loop_execute()` 读取控制器事件并调用 `os_mq_ude_reset_post`、`os_mq_ude_ctl_flow_post`；`ude_control_flow()` 再处理 EP0 状态并通过 `os_mq_ude_ep0_setup_post` 交出 SETUP 包。它们**没有实现 GET_DESCRIPTOR、SET_CONFIGURATION 或 HID 类请求派发器**。默认空钩子无法完成这些工作；SETUP 指针指向可变上下文，异步处理必须先复制八字节。

当前封装通过 `usb_device_enter(4)` 选择 HID 初始化，但该参数不会自动生成或发送描述符。原库的配置头声明 158 字节、五个接口，各模板保留原来的接口编号；HID 接口编号为 4。HID-only 固件必须重建一致的配置长度、接口数和编号，不能直接拼接原配置头和 HID 模板。

现有 [drv_usb.c](../../bare/drivers/src/drv_usb.c) 默认拒绝初始化，直到板级实现覆盖以下接口：

- `drv_usb_platform_init()`：落实时钟、引脚、DMA 可访问内存、代码/数据段、延时与定时器，以及中断和轮询的串行化；具备真实请求处理能力后才返回 true。
- `os_mq_ude_*_post`：保存并分发复位、控制流和 SETUP 工作，处理地址/配置、描述符及类请求。
- `drv_usb_platform_poll()`：推进排队工作，与 `drv_usb_run_loop()` 配合。真实 ISR 不得与轮询并发修改当前端点。

`ude_tmr_isr()` 的倒计时以调用次数计，现有证据没有确定实际周期，不能直接称为毫秒。平台钩子仅改成返回 true 不会补齐这些条件。

## 当前 HID 与音频边界

现有 HID 模板是 **EP `0x83` 的多媒体按键输入**，无 Report ID，报告只有一个字节。位 0–7 分别为快进、下一曲、上一曲、停止、播放/暂停、静音、音量加、音量减。端点描述符最大包为 16 字节，但它没有定义输出报告或通用双向数据协议，也没有 CDC 描述符。

三个长度不能混用：原 `ep3_buf` 只有 8 字节，描述符最大包为 16 字节，而 `udh_init()` 将内部 `max_packet` 设为 `epsize_tbl[3] << 6 = 1024`。内部字段不证明缓冲容量；新封装严格只接受一个字节。

`drv_usb_hid_try_send(buf, 1)` 是新增的非阻塞适配代码，不是声称找回了缺失的厂家发送函数。它检查配置、HID 有效状态和端点状态，复制报告到持久缓冲，每次主循环只推进一次端点传输。发送按键后需另发零报告释放。忙状态清除也可能是复位/中止，不是主机收妥确认；活动传输中止后拒绝复用缓冲，需停止并重新初始化。

原 `usbd_ep3_transmit()` 的倒计时超时不会取消 DMA，没有定时器推进时可能永久等待。通用端点阻塞发送和 IN halt 也存在无超时等待。低层 OUT 接收不提供可靠的缓冲容量检查；短包完成不会推进 `cursor/remaining`，需使用其 `transferred` 字段解释结果。调用者必须在 DMA 完成或确认停止前保留缓冲区。

原 `usb_device_hid_send()` 依赖缺失的 `ude_hid_send()`，目前没有找到其实现或 ROM 地址。封装直接使用已恢复的端点接口；项目的 `--gc-sections` 可移除未使用的旧发送入口。若其他代码保留该入口，仍需真实实现，不能靠空桩解决。

音频模板中的扬声器端点为 OUT `0x02`，48 kHz、双声道、16 位、每包 192 字节；它是接收路径，不能用作 FM 上传。麦克风 IN `0x82` 虽有描述符模板，尚未建立可工作的发送路径。多个音频回调及运行循环为空，故当前 `drv_usb_audio_init()` 和 `drv_usb_audio_send()` 明确返回 false，音量/静音保留兼容空操作。`drv_audio` 传播初始化失败，`app/usb_fm.c` 在启动 FM 前退出。FM→内部 audio buffer 路由仍保留，但不代表 USB 音频链路已实现。Host 模式也尚未实现。

## 构建与验证

从仓库根目录选择恢复实现：

```sh
make -C bare USB_BACKEND=recovered
# 可与已有后端组合
make -C bare SPI_BACKEND=recovered FM_BACKEND=recovered BT_SUPPORT_BACKEND=recovered USB_BACKEND=recovered
```

单独选择 USB 时输出在 `bare/build/recovered-usb/`，默认后端仍为 `vendor`。恢复后端替换上述五个 archive member，工程其他功能仍依赖 SDK。上述为构建入口，不代表完整固件已启动或枚举成功。

差分测试使用 Unicorn 的 RV32 执行器、合成 MMIO 和外部函数钩子，对照原始机器码与恢复 C 编译结果的寄存器访问顺序、状态内存、调用和返回值。厂家自定义 restore 指令使用 ABI 替代；部分阻塞用例通过有界执行确认双方均不返回。结果及源文件/对象 SHA-256 位于：

- [控制器/检测：185 项](generated/recovery/usb-comm-differential.json)
- [端点：391 项](generated/recovery/usb-ep-differential.json)
- [EP0：387 项](generated/recovery/usb-enum-differential.json)
- [HID：103 项](generated/recovery/usb-class-differential.json)

安装 `unicorn==2.1.4`、`pyelftools` 并准备 `clang`、支持 RISC-V 的 `ld.lld`、`llvm-objdump-10`、`ar`、`readelf` 和主机 `cc` 后，可复跑：

```sh
python3 docs/lib-analysis/test_usb_comm_recovery.py --linker /path/to/ld.lld
python3 docs/lib-analysis/test_usb_ep_recovery.py --linker /path/to/ld.lld
python3 docs/lib-analysis/test_usb_enum_recovery.py --linker /path/to/ld.lld
python3 docs/lib-analysis/test_usb_class_recovery.py --linker /path/to/ld.lld
python3 docs/lib-analysis/test_usb_integration.py --linker /path/to/ld.lld
```

集成测试覆盖 SPI/FM/蓝牙/USB 的 16 种后端选择、无效选项、封装队列与缓冲所有权、默认平台拒绝初始化、原导出函数的符号绑定/代码段，以及 vendor/recovered 两种独立 RV32 链接。保留路径无未解析符号，恢复链接未拉入被替换的五个对象。共享音频封装和 shim 修改后，FM 与蓝牙集成检查也通过。

独立链接使用人工地址布局；差分测试不模拟 USB PHY、真实 DMA、总线时序或中断接线。所有结果都属于离线验证，尚未上板枚举、收发或验证完整启动映像。

## 后续工作

1. 完成板级启动、USB 时钟/引脚、DMA 内存和中断/定时器接入，核对实际 USB 数据口连接。
2. 实现一致的 HID-only 描述符和标准/类请求派发器，在 Linux 验证复位、读取描述符、设地址、设配置和 HID 报告。
3. 按通用通信需求增加自定义 HID 双向报告或 CDC 描述符/类请求/端点实现，再做持续收发、短包、断连和重连测试。
4. 单独恢复音频传输与缓冲调度，完成 FM→USB；存储模板需配套块设备与传输状态机后才能使用。
