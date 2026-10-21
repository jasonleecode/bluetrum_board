# Windows Downloader 逆向记录

分析对象为仓库 `windows/downloader/Downloader.exe`（1.9.7）及同目录 `dlls`。Linux 模块使用 Python 标准库；接口说明见 [linux/README.md](linux/README.md)。

## 文件职责和验证

`DeviceUart.dll` 封装 Windows 串口打开、收发和波特率设置。`DeviceRequest.dll` 实现请求编码、CRC、反馈流变换、认证包和加载程序选择。`uartdown-*.dll`、`usbdown-*.dll` 是带 8 字节封装头的芯片端加载程序，并非 Windows 动态库。

`tests/test_bluetrum_uart.py` 使用 Unicorn 执行原版 DeviceRequest.dll 的函数作对照。覆盖所有 48 个请求索引与 14 种上下文组合（672 组）、32 组认证输入、CRC/密钥/RAM 编码以及 17 个加载程序。模拟器只用于开发验证，不是运行 Linux 工具的依赖，也不访问串口。

```sh
PYTHONPATH=/tmp/xmaker-deps python3 -m unittest discover \
  -s tools/tests -p test_bluetrum_uart.py -v
```

## 已恢复的本机函数

下表地址为 DeviceRequest.dll 的 RVA，映像基址 `0x10000000`。

| RVA | 功能 | Linux 实现 |
| --- | --- | --- |
| `0x1000` | CRC16，非反射 0x1021，无最终异或 | `crc16` |
| `0x1060` | CRC32，反射 IEEE，无最终异或 | `crc32` |
| `0x10b0` | 32 位反馈字节 XOR；导出名含 aes，但不是 AES | `xor_stream` |
| `0x1100` | 累加和取负，低 8 位 | `checksum8` |
| `0x1160` | 从 32 位值派生密钥 | `calculate_key` |
| `0x12b0` | 请求索引映射、权限检查、8/9 字节请求编码 | `build_request` |
| `0x1380` | RAM 数据传输变换 | `encode_ram` |
| `0x13b0` | 加载程序解包和明文 CRC 检查 | `unpack_loader` |
| `0x1440` | 18 字节认证包 | `build_authentication` |
| `0x1630` | 芯片标签选择 UART 加载程序 | `loader_name` |

加载程序头是 `dll\0`，`u16@4` 为明文 CRC16，`[6:8]` 被原函数忽略。对 `[8:]` 使用 `CRC16 XOR 0x48502018` 作种子解码，然后校验明文 CRC。CRWN 对应 `uartdown-001.dll`，解码后 9408 字节，CRC16 `a0c2`，SHA-256 `071594ada9d847bd639ad3217c025e9cba5746e09ca7dc8fde0da77e5d81a622`。

请求的明文为 `opcode:u8, value:u32be, selector:u8, count:u16be`。索引 1 强制把 value 设为 `RYAN`，形成 `5a5259414e000000`。常规请求以校验字节和会话密钥派生流种子，产生 9 字节编码帧。具体例外和原程序权限条件以 `linux/bluetrum_uart.py` 为准；DLL 索引不等于线上操作码。

## CRWN 加载程序的交叉证据

以下偏移相对于解包后 `uartdown-001` 文件。根据绝对函数指针与代码位置，加载地址推定为 `0x12000`。入口 `0x0000` 跳转至 `0x0260`，参数结构的 `+0` 为数据缓冲区，`+4` 为已解码请求指针。

| 线上操作码 | 分支偏移 | 从芯片代码确认的行为 |
| --- | --- | --- |
| `0x00` | `0x02c4` | 初始化 Flash 访问函数，返回接口、Flash 类型及操作标志 |
| `0x10` | `0x0652` | 返回 `0x15000` 开始的 512 字节 RAM 数据 |
| `0x11` | `0x0666` | 返回 `00 00 00 01` |
| `0x28` | `0x0b06` | 按地址和块数读出 512 字节块，通过 ROM 发送 |
| `0x2a` | `0x0b64` | 从 ROM 接收 512 字节块，调用 Flash 写函数 |
| `0x2d` | `0x03a4` | 擦除分支，支持 4 KiB / 64 KiB 单元和预先空白检查 |
| `0x2f` | `0x0bec` | 按块返回 CRC16 或 CRC32 列表 |

在上述加载地址下，ROM 调用地址推定为发送 `0x80068`、接收 `0x8006c`、CRC16 `0x80078`。这些静态代码证据已与下面恢复的主机状态机交叉核对，仍缺少实机串口记录。

## 托管方法恢复

Downloader.exe 的关键方法在磁盘上是空壳，真实 IL 保存在保护资源中。初始化器 MethodDef RID 9379，RVA `0x263d4`，包含资源变换和运行时方法恢复代码。

已通过静态计算解开方法资源：2837 处 DWORD 补丁、8240 条 IL 方法记录，解析恰好消耗 811945 字节，没有剩余或越界。后续协议结论应引用恢复后的 MethodDef RID 和 IL 偏移，不能把磁盘空壳当作真实实现。

可复现脚本 `research/recover_downloader_methods.py` 仅分析文件，不启动 Windows 程序。它严格匹配源 EXE SHA-256，解出记录并检查数量、边界和重复项。以下额外依赖仅用于研究，不是 Linux 下载器运行依赖：

```sh
python3 -m pip install --target /tmp/downloader-research dnfile dncil cryptography
PYTHONPATH=/tmp/downloader-research python3 tools/research/recover_downloader_methods.py \
  tools/windows/downloader/Downloader.exe /tmp/downloader-analysis --il --booleans
```

输出是方法记录、IL 文本和布尔表，不是可执行 EXE。显示的 IL 偏移统一采用 12 字节合成方法头；原始 locals/异常处理元数据不包含在显示文本里。

## 普通 UART 下载流程

| 方法 RID | 恢复行为 |
| --- | --- |
| 1244 | 115200 路径的物理同步：发送 `a596875a`，滑窗匹配 `5a6978a5` |
| 1253 | 发帧 `4b,seq,lenLE16,payload,CRC16LE16`；回复 `status,seq`，`1e/3c` 成功、`2d` 重试 |
| 1255 | 请求回复 `b4,seq`，接收 `4b,seq,lenLE16,payload,CRC16LE16`，每块最多 512 字节 |
| 534 / 532 | 请求索引 1，selector=1，随机 nonce；24 字节回复包含 `BLUE`、芯片、版本、RAM 地址及 challenge |
| 535 | 请求索引 3 协商/更新会话密钥；519 每十次受计数的传输后更新密钥 |
| 549 | 普通 UART 用索引 5 写 RAM、4 读回验证、6 跳转；不是独立 RAM 下载模式的 541 |
| 527 / 551 / 553 / 528 | 初始化 Flash、读取设备信息、读 JEDEC ID 并查容量；保留设备报告的权限 |
| 1452 | 默认镜像要求 Flash 除 body 外至少剩余 20,480 字节 |
| 1463 | 按 64 KiB / 4 KiB 擦除，每次擦除后 534(0) 确认；从地址 512 起写入 |
| 1464 / 1466 | 逐 512 字节 CRC16 比较，CRC 请求最多 64 KiB 且在 SEG 边界切分 |
| 1465 | 其余数据校验通过后才写首 512 字节；再校验前缀至首 SEG 边界/64 KiB，更新密钥 |
| 1467 / 566 | 请求索引 22 结束下载/重启；这个命令不触发后续计数刷新 |

默认 DCF 没有额外 XCFG 预处理：直接传输 body 原字节。应用段内的块使用 selector=1，其他块使用 0；CRC 返回每块的小端 16 位结果。Linux 擦除规划按 4 KiB 对齐，容量检查也按该边界加 20 KiB，比原版 body 长度检查略严格。

当前实现仅覆盖 CRWN ROM `01000000`、普通 UART 115200 路径。不实现专用适配器、高速切换或权限认证；回显会检查内容，丢失 ACK/错序/CRC 错误时直接终止，明确 NACK 才有限重试。这些限制与原版完整 GUI 支持范围不同。

`bluetrum_serial.py` 负责 Linux 8N1 串口传输、波特率切换及有截止时间的读写，伪终端测试覆盖原始字节、分片回复、超时和关闭时恢复设置。它本身不选择芯片命令，也不自动扫描端口。
