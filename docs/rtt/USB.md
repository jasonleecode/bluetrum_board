# 原生 USB CDC 软件适配

综合 RTT 已加入 AB32 USB Device 控制器适配和 CDC ACM 双向字节回传示例。
**已完成软件适配、离线测试和编译；后续按键接入版综合固件已烧录，尚未验证 USB 枚举或真实收发。**
当前开发板为 V2.0，已烧录包含 USB CDC、FM、蓝牙、TF、红外和按键的综合固件。
原生 Device 实测前还需要确定合适的硬件引出与供电方案，并非仅缺一根数据线。

## 接线

### V2.0 接口与数据线走向

依据 [V02 原理图](../ab32vg1_doc/AB32VG1_Prougen_Schematic_V02.pdf) 的
Power&Type-C、X-LINK 和 USB 三个区域，两个接口的数据通路如下：

| 板上接口 | 原理图数据通路 | 用途与边界 |
| --- | --- | --- |
| Type-C | D− 经 R11 接 DM1 → U1 CH340E 的 UD−（2 脚）；D+ 经 R13 接 DP1 → UD+（1 脚） | 电脑枚举的是 CH340 串口桥，供 UART0 下载和 shell 使用，通常为 `/dev/ttyUSB0`；不直接连接 AB32 的 USB 控制器 |
| USB-A | D−（2 脚）经 USB_DM、P5 接 PB4；D+（3 脚）经 USB_DP、P5 接 PB3 | 板级用于 Host 的接口，原生 USB 数据线在这里；USB-A 的 1 脚直接接板上 +5V |

Type-C 上的 **DP1/DM1** 与 USB-A 上的 **USB_DP/USB_DM** 是不同网络。
Type-C 虽然作为 USB Device 接电脑，但该设备是 CH340，不能据此认为 AB32 原生 CDC 走 Type-C。

P5 是两条数据线的连接跳线，不是 Host/Device 模式选择器：

| P5 短接对 | 芯片侧 | USB-A 侧 |
| --- | --- | --- |
| 1–2 | 2 脚：PB4 / D− | 1 脚：USB_DM |
| 3–4 | 4 脚：PB3 / D+ | 3 脚：USB_DP |

短接这两对仅将 AB32 数据线接到 USB-A，**不会将其切换到 Type-C**。
[旧版用户手册 §2.9](../ab32vg1_doc/AB32VG1_User_Manual.pdf) 中的 J15/J16
属于旧版标号，不应作为 V2.0 的跳线位置说明。

### 原生 Device 测试前提

此前“短接 P5 后直接把独立 USB 口接电脑”的步骤不完整，现予以更正。
AB32 控制器的软件 Device 模式不会改变板上的接口布线或 +5V 连接。
**不要使用普通 USB-A 双公线直接连接电脑和板上 USB-A**：该口的 VBUS 接板上 +5V，
不能忽略两侧供电相连的问题。

后续需确定从 PB3/PB4 引出 D+/D−、共地并接入合适 Device 接口的方案，
同时核对 VBUS、板上供电及所选接口的配置要求；P5 的芯片侧引脚可作为方案评估点。
这项硬件适配尚未实施或验证，当前文档不将它视为仅装跳线帽即可使用的 Device 接口。
原 Type-C 可继续用于 CH340 下载及 shell，但须在硬件方案中一并核对供电路径。
PB3/PB4 在 USB 工作期间由 USB 独占，不能同时作为其他 GPIO 使用。

## 构建和启动

默认 `BSP_USING_USB_CDC` 已编入综合工程，上电不自动连接 USB。
使用仓库已有 CherryUSB **1.5.2** 的核心和 CDC 类代码；没有新增子模块，也不链接厂家 USB 对象。

```sh
# 仓库根目录；综合工程还需要 NimBLE 子模块
git submodule update --init rtt/nimble
cd rtt/bsp
scons -j4
```

生成 `rtthread.elf`、`rtthread.bin`，DCF 打包与下载见 [BSP 说明](../../rtt/README.md#linux-构建与驱动维护)。
当前板上已有该功能；完成上述 Device 接口及供电适配后，在原 CH340 串口 shell 中执行：

```text
usbcdc start
usbcdc status
free
```

`start` 在 USB 线程中完成硬件初始化；`running=1` 只代表初始化成功，
`configured=1` 才代表主机已设置配置，仍需读写验证。USB 时钟来自当前 BSP 的
240 MHz PLL0 五分频；若 PLL0 配置不符，初始化返回错误，不擅自改变 CPU/蓝牙共用 PLL。

预期 Linux 枚举为 CDC ACM `/dev/ttyACM*`，产品名 `AB32 CDC`。
当前使用仅供台架调试的 **VID:PID `ffff:ab32`** 和固定序列号 `AB32-CDC-0001`；
它不是项目获分配的生产标识，多板部署前也需改为唯一序列号。
可设置并读回 CDC 波特率、停止位、校验和数据位；这些只是虚拟串口参数，
不会改变 UART0 的 115200 配置。当前 CDC 接口是**二进制原样回传**，未接管 FinSH 控制台。

```text
usbcdc stop
usbcdc status
usbcdc start
```

停止会关闭 USB、取消传输并恢复占用的引脚/时钟配置；工作线程及静态池保留供重启复用。
USB 不切换 FM/蓝牙的射频使用权，但 USB 与这些功能同时运行的硬件时序和内存压力尚未测试。

## Linux 验证步骤（待 Device 硬件接入就绪后执行）

先观察 `lsusb` 和 `dmesg`，确认新出现的 `ffff:ab32` 及实际 `/dev/ttyACM*` 节点。
然后在仓库根目录执行：

```sh
python3 tools/rtt/test_usb_cdc.py --port /dev/ttyACM0 --rounds 3 --output /tmp/ab32-usb-cdc.json
```

脚本先通过 sysfs 校验 VID/PID，拒绝把 CH340 下载串口当作 CDC 测试。
它用 Linux 标准串口 API 验证 1、7、63、64、65、127、128、129、1024、4097 字节的
二进制回传，包含零字节和换行，循环打开/关闭串口并保存成功结果。
这是准备好的测试工具，**当前未在真实 USB 上运行**。物理拔插、USB 总线复位、
持续传输、系统挂起/恢复及与 BLE/FM 的组合还需要单独验证。

## 实现与离线测试

- `bsp/board/ports/usb/usb_dc_ab32.c` 根据恢复的 `usb_comm.o`、`usb_device_ep.o`、
  `usb_device_enum.o` 和生命周期寄存器序列实现 CherryUSB DCD。AB32 是 32 位间隔的
  寄存器及 DMA 前端，不能直接套用通用 MUSB FIFO 驱动。原恢复文件保持不变。
- 一个 2048 字节栈、优先级 12 的 RTT 线程每 1 ms 轮询一次，串行执行控制器及协议回调；
  CPU 的 USB IRQ 保持屏蔽，因此 USB 代码可放 Flash。当前不声称达到某一吞吐率。
- DMA 使用持久、对齐的 64 字节中转缓冲，复制前检查长度；Flash 描述符不会直接送 DMA。
  支持 EP0、EP1 IN、EP2 OUT、EP3 IN，最大包 64 字节；其他端点/等时模式会拒绝。
- 覆盖 SETUP 中止旧请求、延迟 SET_ADDRESS、IN/OUT 状态阶段、满包与零长度包、
  STALL/清除、配置取消及复位。控制器层不使用原库的无限忙等。
- CDC 数据在 IN 及必要的 ZLP 完成之前不重用 OUT 缓冲，以 NAK 保持背压。
  CDC 请求入口校验方向、接口和长度，避免旧版类处理器按未验证长度复制行编码结构。
- 不支持 USB Host、USB 音频、MSC、远程唤醒或控制台切换。

```sh
python3 tools/rtt/test_usb.py
python3 tools/rtt/test_drivers.py
python3 tools/rtt/check_builds.py usb nimble bluetooth audio
python3 tools/rtt/memory_report.py
```

USB 离线测试将真实 CherryUSB 核心、CDC 类和新适配层链接到模拟寄存器/DMA 环境，
开启 AddressSanitizer/UndefinedBehaviorSanitizer。已通过描述符、多包 EP0、地址生效时机、
行编码、非法请求、0–64 字节回传、背压、ZLP、越界拒绝、清除 STALL 后恢复、复位和重启。
如果 LeakSanitizer 所在环境使用 ptrace 沙箱，需在允许其运行的环境执行；测试代码不访问真硬件。
八组原有驱动测试、USB/NimBLE/仅控制器/音频四种构建均通过。

首次 USB 接入阶段的综合固件 `.heap` 原始区域为 **28320 字节**，`.bss` 为 **11120 字节**，
USB 等线程代码加入后 `.flash` 为 **64512 字节**。带 USB 的原始堆下限设为 27 KiB；
未启用 USB 时仍保持原先的 NimBLE 28 KiB / 仅控制器 39 KiB 检查。
第二 SRAM 仍只余 24 字节。USB 开始运行还需分配线程栈和线程对象，
这些链接数值不代表运行时 `free`；完整 PCM 池与 NimBLE、USB 同时启用需重新评估。
详见 [构建/测试记录](../lib-analysis/generated/recovery/usb-rtt-software.json)。
以上为该阶段历史数据，当前含 TF、红外和按键的综合固件预算见 [MEMORY.md](MEMORY.md)。
