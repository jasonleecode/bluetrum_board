# Xmaker / DCF 逆向记录

Linux 打包实现已恢复仓库使用的 `make(header, app)` 默认路径，不需要 Windows、Wine 或第三方 Python 包。入口及使用方法见 [linux/README.md](linux/README.md)，串口烧录实现另见 [DOWNLOADER.md](DOWNLOADER.md)。

## 验证依据

`tests/xmaker_oracle.py` 在 Unicorn 中直接执行原版 Windows EXE 的 x86 打包指令，入口 `0x40ea66`，终点 `0x4104ac`。CRC、流变换、缓冲区操作和打包逻辑均执行原码；只替代内存分配、memset 和打印。构造器 `0x4082f8–0x408327` 将脚本配置置零，作为默认路径的配置来源。

`tests/xmaker_oracle_evidence.json` 保存 20 组逐字节对照结果和 SHA-256，应用长度覆盖 1 字节到 1 MiB，包括 512 字节、64 KiB 和 CRC 表扩展边界。另有 `xmaker_oracle_limit_evidence.json` 验证 API 上限：应用 16,703,488 字节，body 恰好 16 MiB。Downloader 的 RID 1441 在 IL `0x0984–0x098f` 拒绝 body 大于 16 MiB；因此输入上限还要扣除 header 区域和 XCOD 表。普通测试使用 20 组原版结果作参考，不依赖模拟器；该格式上限不代表开发板有相应 Flash 容量。可选的原码复核：

```sh
python3 -m pip install --target /tmp/xmaker-deps unicorn
PYTHONPATH=/tmp/xmaker-deps python3 tools/tests/verify_xmaker_oracle.py
PYTHONPATH=/tmp/xmaker-deps python3 tools/tests/verify_xmaker_oracle.py --large
```

源文件指纹：

| 文件 | SHA-256 |
| --- | --- |
| `windows/riscv32-elf-xmaker.exe` | `12ea6b09dd9036485c3a5f7e6762d4ff49a37b711d2f2bbfeff1f9193e908822` |
| `header.bin` | `08211dd825b8df025cf297a27269d4fd3cd6aadbc39c667d44c81ed7ceb6747c` |

这验证的是默认打包路径的字节兼容性；没有执行完整 Windows 进程或原版 XM 文件解析器，也没有完成上板烧录测试。

## 默认 DCF 外层格式

整数均为小端。标签除文件开头 `DCF\0` 外，每个非零 ASCII 字节异或 `0x80`，零字节保持不变。

| 文件偏移 | 内容 |
| --- | --- |
| `0x00` | `DCF\0`；`u32@0x04 = 文件长度 - 8` |
| `0x08` | `XEAD`，长度 64 |
| `0x10` | `INFO`，长度 8；内容是解码 header 的 `[4:12]`，本仓库为 `CRWN` + 版本 1 |
| `0x20` | `DEV\0`，长度 8；默认配置为全零 |
| `0x30` | `SEG\0`，长度 12；段数 1、应用起始偏移、结束偏移；偏移相对于 body |
| `0x44` | `CRC\0`，长度 4；`u16@0x4c = CRC16(body)`，`u16@0x4e = CRC16(file[0:0x4e])` |
| `0x50` | `DATA`；`u32@0x54 = body 长度` |
| `0x58` | body 开始 |

默认脚本没有 `KEY` 记录和最终整包 XOR。此前实验实现误将加密 header 的内容当作脚本配置，导致 `DEV/KEY`、外层长度和加密行为错误；现已修正，旧实验输出应重新生成。

## 内层数据

输入 header 固定 4096 字节，先以 `XFIL` 常量 `0x4c494658` 解码。镜像前段扩展至 8192 字节，`XCOD` 记录从 body 的 `0x2000` 开始。

应用按 512 字节分块，末块补 **零**。XCOD 头为 16 字节，后接每块 CRC16；整个表按 512 字节对齐。剩余表项不是填充 `0xff`，而是用当前绝对偏移作种子，对此前记录字节计算 CRC16 并逐项追加。每个应用块的 CRC 种子为块序号加一。

默认路径还生成 body 的 `DOWN` 区域、应用信息 CRC、顶部和主区域 CRC，再分别执行恢复出的流变换。应用块密钥来自主区域 CRC（解码顶部的 `0x1c`）和每块 CRC，不是应用信息 CRC（`0x80`）。完整操作顺序以 `xmaker_re.py` 为准。

CRC16 是非反射 `0x1021` 多项式，默认种子 `0xffff`，无最终异或。原程序所谓的加密是基于 32 位反馈状态的字节 XOR，不是 AES；查表可直接生成，日常打包不再读取 EXE。

## 支持范围

- `build_dcf(header, app)` 支持仓库固定 header 和非空原始应用。
- Linux XM 入口支持 `make`、`save`，接受 UTF-16 BOM 或 UTF-8，路径相对于脚本目录。
- 自定义 header、资源/配置/KEY 选项以及其他 XM 命令明确报错，不会静默忽略。
- `linux/bluetrum_dcf.py` 校验默认外层布局、长度、CRC 和 XCOD/SEG 对应关系。校验通过不代表应用入口、链接地址、容量适合某块开发板。
- `xmaker_re.py` 的旧 `inspect-body/inspect-dcf` 子命令保留作研究输出；正常使用请采用 Linux 入口的严格检查。
