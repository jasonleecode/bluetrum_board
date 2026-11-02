# TF 卡软件适配

TF 已作为模块编入同一综合 RTT 工程，与 FM、恢复源码的蓝牙控制器、NimBLE 和 USB CDC 一起构建。
**目前没有 TF 卡：仅完成软件适配、离线验证和编译，未烧录此版本，未完成实卡读写。**

## 接线与配置

按仓库 [V02 原理图](../docs/ab32vg1_doc/AB32VG1_Prougen_Schematic_V02.pdf)，
板载卡槽使用 SD0 G2 的一位原生 SD 总线，不是 SPI 模拟：

| 信号 | 引脚 | P2 跳线 |
| --- | --- | --- |
| CMD | PB0 | 1–2 |
| CLK | PB1 | 3–4 |
| DAT0 | PB2 | 5–6 |
| DET | PE5 | 7–8；本版未读取检测开关 |

旧版手册 §2.11 对应 J6/J7/J9/J11；以实际板卡丝印和原理图为准。
PB0/PB1/PB2 由 TF 独占，不能同时启用旧 SDIO 驱动、PB0 的 T3 PWM 或 SPI1 G3。
USB 的 PB3/PB4 与它不同，但并行传输、射频干扰和供电仍需实测。

默认启用 `BSP_USING_TFCARD`、DFS v1 和 Elm FatFs。旧的 `BSP_USING_SDCARD` /
`BSP_USING_SDIO` 配置保留用于单独回归，不能与新端口一起使用。
支持 FAT12/16/32、一个挂载卷和 8.3 文件名；暂不支持长文件名、中文名、exFAT、热插拔或 USB MSC。
SDXC 卡的块协议与 exFAT 是两回事：出厂为 exFAT 的卡不能直接挂载。
本版 RTT 的有符号 RV32 块地址限制卡容量为最多 `INT32_MAX` 个 512 字节扇区，超出会拒绝。

## 后续有卡时使用

编译和打包：

```sh
cd rtt/bsp
scons -j4
cd ../..
tools/linux/convert rtt/bsp/header.bin rtt/bsp/rtthread.bin rtt/bsp/rtthread.dcf
```

待有卡后按 [下载说明](README.md#linux-构建与驱动维护) 烧录，用原 CH340 串口 shell 操作：

```text
tf status
tf probe
tf mount
ls /
cat /TEST.TXT
free
ps
tf umount
tf stop
```

上电只注册块设备 `tf0` 和静态互斥锁，不访问卡、不创建 SD 后台线程。
`probe` 初始化并打印扇区数、时钟、写保护和底层错误码；无卡或通信失败会有限时退出。
`mount` 要求先成功探测，将已有 FAT 卷以读写方式挂到 `/`，不会自动格式化。
没有根文件系统时无需先建 `/sd`；本版只挂载一个卷。
应用可使用 RTT `rt_device_read/write`（单位为扇区）或 DFS `open/read/write/fsync/close`。

退出时先关闭文件，再 `tf umount`，最后 `tf stop`，然后拔卡。
设备仍打开时拒绝重新探测和停止。传输故障使设备失效，后续 I/O 返回错误；
不在已挂载卷下面自动切换新卡。关闭文件、卸载后才能重新探测。
`stop` 停止 SD0，不切断卡供电，也不将引脚自动转交其他外设。
没有启用检测脚，因此 `ready=1` 是缓存的初始化状态，不能用它判断拔卡。

`error` 使用 [底层头文件](../bare/drivers/inc/drv_sdcard.h) 的枚举：
例如 3=无卡、4=超时、5=命令 CRC、7=数据 CRC、9=写保护、11=不支持、12=DMA 缓冲错误。
未接检测脚时，无卡也可能表现为响应错误/超时；不能仅凭错误码判断接线正常。

## 实现与验证范围

- 直接编译已有 `bare/drivers/src/drv_sdcard.c`，新适配层在 `bsp/board/ports/tf/drv_tf.c`。
  没有新增厂家 `.a` 依赖；底层依据本地寄存器手册实现，详细范围见 [SD 恢复记录](../docs/lib-analysis/SD_RECOVERY.md)。
- 使用实际 `get_sysclk_nhz()`（默认 48 MHz），识别阶段不超过 400 kHz，传输目标 12 MHz。
  时钟改变后拒绝继续 I/O；120 MHz 源时钟不能用现有八位分频器满足识别频率，会被底层拒绝。
- 独立 512 字节、32 字节对齐的静态 DMA 中转区位于主 SRAM。运行时检查地址，
  链接检查同时验证范围/对齐，不截断任意调用者指针；允许调用者缓冲未对齐。
- RTT 1 kHz tick 提供真实单调时间，换算为微秒，实际分辨率 1 ms；等待让出线程。
  命令默认超时 100 ms、数据默认 1 s，调度延迟会影响实际返回时间；不能从这些值推断吞吐率。
- 全部底层操作经一个互斥锁串行化，多扇区请求逐块完成；部分失败返回成功前缀，
  CRC 失败的读块不复制到调用者。写失败可能已改变介质，不能当成事务回滚。
- 每次写入等待卡退出忙状态；SYNC 没有待刷新驱动缓存，不额外探测卡。
  修复共享 DFS/FatFs glue 的几何查询与 SYNC 错误吞掉问题，避免几何失败时除零；
  系统时间未设置时使用有效的 FAT 日期 2020-01-01，而非下溢年份。

```sh
python3 tools/rtt/test_tf.py
python3 tools/rtt/test_drivers.py
python3 tools/rtt/test_usb.py
python3 tools/rtt/check_builds.py tf usb nimble sd
python3 tools/rtt/memory_report.py
```

`test_tf.py` 使用真实 TF 适配层、DFS disk glue 和 FatFs，在进程内分配的模拟卡上测试
FAT12 无分区卷与 FAT16/32 分区卷的格式化、写入、同步、卸载重挂、读回，
以及未设置时钟时的文件日期和 CRC 错误上传。
它还测试无卡超时、未对齐读写、越界、部分读写失败、写保护、已打开设备的探测拒绝、
时钟变化和容量限制，并重跑底层 SD0 的 **34 场景、1217 断言**；启用 UndefinedBehaviorSanitizer。
测试不访问真实磁盘或串口，格式化仅发生在内存模拟卡上。

综合链接的原始堆区为 **24352 字节**，SRAM1 使用 31200 / 31232 字节。
这是链接预算，不是开机空闲堆；文件系统挂载、打开文件、BLE、USB、PCM 都另有运行时分配。
保留完整 PCM 池再同时启动 BLE/USB/TF 的余量不足以据此保证，需按组合测试并重新预算。
本版没有为适配 TF 缩减蓝牙固定区、IRQ 栈或已有线程栈。详见 [内存记录](MEMORY.md)。

实卡阶段待验证：无卡启动和探测退出、SDSC/SDHC 初始化、容量与分频、DMA/CRC/忙等待、
已有文件读回、新建文件写入并断电后核对、卸载/重新插卡、长时间读写，以及 BLE/USB/FM 共存。
