# Linux 工具

需要 Python 3.9 或更新版本。普通打包和检查仅使用标准库，无需 Windows、Wine 或 pip 依赖。

已实现 **DCF 打包和串口下载命令行**。打包结果通过原版机器码逐字节对照；串口协议从下载器原始 DLL 和解密后的 .NET 方法恢复，已通过离线测试，**尚未上板验证**。

## 生成 DCF

以下命令在仓库根目录执行，输入 `.bin` 应先由对应工程编译生成：

```sh
tools/linux/convert tools/header.bin bare/build/main.bin /tmp/main.dcf
python3 tools/linux/bluetrum_cli.py inspect /tmp/main.dcf
```

`convert` 接受三个位置参数：header、原始应用、输出 DCF。仅支持仓库自带的 header 和默认打包配置；输出通过临时文件原子替换。

也可以执行现有 XM 脚本：

```sh
cp bare/build/main.bin tools/user.bin
tools/linux/riscv32-elf-xmaker -b tools/final_bin.xm
```

结果位于 `tools/final_bin.dcf`。支持 UTF-16 BOM 和 UTF-8；文件路径相对于 XM 所在目录，带空格的路径用双引号包住。例如：

```text
make(dcf_buf, header.bin, "my app.bin");
save(dcf_buf, "my app.dcf");
```

RT-Thread 的打包脚本也可单独执行：

```sh
tools/linux/riscv32-elf-xmaker -b rtt/bsp/rtthread.xm
```

原版 `download.xm` 用于进入 Windows 下载器界面；Linux 请使用下述显式串口命令。XM 中的 `download` 和尚未支持的资源/配置命令会在写入输出前报错。

## 串口烧录

当前支持仓库对应的 **CRWN、ROM 版本 `01000000`、115200 波特率**。先检查镜像和擦除范围，`--dry-run` 不会打开串口：

```sh
tools/linux/downloader flash /tmp/main.dcf --dry-run
```

读取芯片信息（运行后按板上复位键，让芯片进入下载模式）：

```sh
tools/linux/downloader probe --port /dev/ttyUSB0
```

烧录时使用明确指定的串口：

```sh
tools/linux/downloader flash /tmp/main.dcf --port /dev/ttyUSB0
```

默认按原版普通 UART 路径接收发送回显；如果适配器不回显，添加 `--no-echo`。不自动扫描端口，不依赖 Windows 串口 DLL。`--connect-timeout` 控制等待复位的时间；`--no-reboot` 可在成功校验后保持下载模式。

流程为：ROM 握手 → 上传并读回核对 RAM loader → 识别 Flash → 检查型号、容量和操作权限 → 擦除 → 写入并校验 → 最后提交首 512 字节 → 重新校验前缀 → 重启。CRC 校验按应用段边界分组，首块不会在其余内容验证失败时提交。

擦除范围从地址 0 起，按 4 KiB 向上取整，完整对齐范围使用 64 KiB 擦除；范围内的旧数据会被替换。容量检查还保留原下载器要求的 20 KiB 空间。校验失败、响应错序或应答超时会终止；仅收到明确 NACK 时重发同一序号的数据帧。

当前不支持 USB 下载、专用 BLUE 适配器、高速波特率切换及受限设备的权限认证。设备未授予写入、擦除和 CRC 权限时直接报错。目标端 RAM loader 仍使用仓库自带的 `uartdown-001.dll` 固件容器；Linux 主机端代码已经重写，不运行其中的 Windows 程序。

## 加载程序分析

Windows 下载器的 `uartdown-*.dll` 实际上是封装过的芯片端 RAM 程序。恢复出的解包算法会检查明文 CRC。本仓库 `CRWN` 对应 `uartdown-001.dll`：

```sh
tools/linux/downloader inspect-loader tools/windows/downloader/dlls/uartdown-001.dll
tools/linux/downloader unpack-loader tools/windows/downloader/dlls/uartdown-001.dll /tmp/uartdown-001.bin
```

上述加载程序分析命令不会打开串口。`bluetrum_uart.py` 提供请求编码和校验等基础算法，`bluetrum_download.py` 实现连接及传输状态，`bluetrum_flash.py` 实现擦写和提交顺序。

## 验证

```sh
make -C tools/linux check
```

测试覆盖原版 DCF 参考结果、XM 编码与路径、命令行入口、损坏镜像拒绝、串口伪终端、协议应答脚本及模拟 Flash 的校验失败处理。原版机器码对照需要单独安装 Unicorn，仅用于开发验证；参见 [../XMAKER.md](../XMAKER.md) 和 [../DOWNLOADER.md](../DOWNLOADER.md)。模拟测试不等于串口实机验证。
