# NimBLE 接入与验证

NimBLE Host 已接入综合 RTT 工程，与 FM、PCM、串口和 LED 一起构建。
控制器继续使用已恢复的源码和片内 ROM，不链接 `libbtctrl.a`。

## 源码与构建

`rtt/nimble` 是 [Apache Mynewt NimBLE](https://github.com/apache/mynewt-nimble)
的 Git submodule，固定到 `nimble_1_10_0_tag`：
`a7a156f28954819e158b62dd613008f22f9cf73b`。未修改上游源码。
在仓库根目录执行：

```sh
git submodule update --init rtt/nimble
cd rtt/bsp
scons -j4
```

默认 `BSP_USING_NIMBLE` 启用 Host，并依赖 `BSP_USING_BTCTRL`。
RT-Thread 移植代码、配置和演示服务位于 `bsp/board/ports/nimble/`。
使用项目自带的 submodule，不再使用旧 `PKG_USING_BLUETRUM_NIMBLE` 包或旧心率示例。
禁用 Host 后仍可构建和运行控制器诊断。

## 串口与 GATT

烧录所用 UART0 同时提供 shell，115200、8N1。上电不自动启用射频：

```text
ble start
ble status
free
```

启动后广播名称 `AB32-NIMBLE`，固定测试随机地址 `C2:00:AB:32:00:02`。
支持一个 BLE 外设连接；断开后自动重新广播。多块板同时测试时需分配不同地址。

| 项目 | 值 |
| --- | --- |
| 自定义服务 UUID | `ab320001-5e7f-4c9a-9b7b-75ba2a2f0001` |
| 特征 UUID | `ab320001-5e7f-4c9a-9b7b-75ba2a2f0002` |
| 属性 | Read、Write（有响应）、Notify |
| 初值 | `AB32 RTT NimBLE`，15 字节，不含末尾 NUL |
| 存储上限 | 64 字节 |
| 首选 ATT MTU | 64；普通 Write/Notify 有效载荷最多 MTU−3，即 61 字节 |

写入会更新读值，并通知已订阅的客户端。实测载荷为 1、20、60 字节；
60 字节覆盖了控制器 27 字节 ACL 数据包的分片与重组。尚未验证长写入及高吞吐场景。
这是**未加密的测试服务**；当前配置关闭配对、绑定、加密及隐私功能，未启用
Central、Observer、扩展广播、周期广播、ISO 或 Mesh。

`ble stop` 仅停止当前广播，`ble adv` 恢复广播；两者不释放 Host/控制器或断开现有连接。
断开连接仍会自动广播。`ble start` 与 `btctl test` 二选一，占用后切换需复位。
蓝牙启动前会停止 FM；蓝牙占用射频后 FM 启动返回忙，复位后才可重新使用 FM。
这些模块属于同一个固件，目前尚不支持 FM 与蓝牙同时使用射频。

## 移植约束

- NPL 使用 RT-Thread 信号量、互斥锁、侵入式事件队列及软定时器。重复事件不重复入队，
  队列没有额外的固定指针数组上限；所有 Host 回调在线程中运行。
- NimBLE Host 线程栈 3072 字节，控制器线程栈 2048 字节。HCI 命令和 ACL
  经队列交给现有控制器线程执行，避免多个线程直接进入控制器。
- 控制器 RX 缓冲只在回调期间借用，先复制到 NimBLE 池再确认释放。
  TX ACL 使用六个临时槽，入队后立即释放 Host mbuf，避免占住唯一分片缓冲。
  队列拒绝、包长度错误和关键事件池耗尽都有错误处理及离线测试。
- 普通 Host、GATT 和传输代码放 Flash；NPL、临界区、mbuf 队列/池、内核和控制器
  中断路径保留 SRAM。局部 assert 映射到 RT_ASSERT，避免拉入 newlib stdio/exit。
- 固定控制器 ABI RAM `0x11000..0x137ff` 保持原位。Host 静态池放普通 BSS，
  包括 MSYS 8×128、ACL RX 六项及普通/可丢弃 HCI 事件 4/2 项（事件上限 257 字节）。

## 验证方法

仓库根目录：

```sh
python3 tools/rtt/test_drivers.py
python3 tools/rtt/check_builds.py nimble bluetooth audio
python3 tools/rtt/memory_report.py
```

八组离线驱动测试通过；NimBLE、仅控制器、音频配置均完整构建通过。
新增测试覆盖事件 FIFO/去重/移除/唤醒/超时、tick 回绕、软定时器、RT 对象名限制，
以及 HCI 缓冲所有权、分片及时释放、队列/池耗尽和包边界校验。

电脑端需要可用的蓝牙适配器、BlueZ，以及系统 Python 的 `python3-dbus`、`python3-gi`。
板上先执行 `ble start`，再运行：

```sh
/usr/bin/python3 tools/rtt/test_nimble_gatt.py --rounds 3 --output /tmp/ab32-nimble-gatt.json
```

脚本仅连接指定开发板，验证服务发现、读写、真正的通知、断开与重连。
通知必须在 ReadValue 前收到，避免把 D-Bus 的读取属性变化误认成广播通知。
当前主机 BlueZ 5.53 已通过测试。`ble status` 的正常主动断开原因 531 是
`BLE_HS_HCI_ERR(0x13)`，不代表 Host 重置或传输失败。

最终实机结果见
[nimble-rtt-hardware.json](../lib-analysis/generated/recovery/nimble-rtt-hardware.json)
和 [串口日志](../lib-analysis/generated/recovery/nimble-rtt-hardware.log)。
当前内存数据见 [MEMORY.md](MEMORY.md)；长期稳定性、手机兼容性和安全配对未验证。
