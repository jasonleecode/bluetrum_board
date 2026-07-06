# AB32VG1 裸机工程

`bare` 目录是不带操作系统的 AB32VG1 示例工程，不依赖 RT-Thread，也没有线程、调度器、设备框架等运行时。程序从 `startup.S` 进入 C 入口后直接执行 `app` 下的业务代码，适合验证芯片外设、闭源库接口和小型单任务程序。

由于 AB32VG1 官方提供的驱动库是闭源的，本工程基于对官方 `libhal.a` 的分析，整理出部分驱动接口，并在 `drivers` 目录下做了简单封装。

## 目录说明

- `app/`: 裸机应用示例。每个 `*.c` 文件会被 `Makefile` 编译成一个独立目标。
- `drivers/`: 对 UART、ADC、FM、USB、SPI、I2C、Timer 等外设接口的轻封装。
- `lib/`: 厂家闭源静态库，例如 `libhal.a` 和 `libbtctrl.a`。
- `startup.S`: 裸机启动代码。
- `link.ld`: 裸机链接脚本。
- `Makefile`: 使用 RISC-V GCC 工具链构建 `app` 下的示例程序。

## 编译

默认工具链前缀是 `riscv64-unknown-elf-`，如需使用其他工具链，可以通过 `CROSS_COMPILE` 覆盖：

```sh
make
make CROSS_COMPILE=riscv64-unknown-elf-
```

编译后会在 `build/` 下生成对应的 `.elf` 和 `.bin` 文件。例如 `app/main.c` 会生成：

```text
build/main.elf
build/main.bin
```

清理构建产物：

```sh
make clean
```

## 生成下载文件

裸机程序编译完成后，`build/*.bin` 不能直接交给串口烧录工具烧写。它只是用户程序输入文件，需要先使用厂家提供的 `riscv32-elf-xmaker` 工具，将用户程序 `bin` 和固定的 `header.bin` 合成为最终的 `.dcf` 下载文件。

仓库顶层 `tools/final_bin.xm` 的逻辑是将 `header.bin` 和 `user.bin` 合成为 `final_bin.dcf`。使用时可以先把裸机目标 `.bin` 准备成 `user.bin`，再执行：

```sh
riscv32-elf-xmaker.exe -b final_bin.xm
```

生成的 `final_bin.dcf` 再交给厂家 Windows 版 `Downloader.exe` 通过串口烧录。`tools/XMAKER.md` 记录了目前对 `.xm` 脚本和 `.dcf` 合成格式的分析。

`rtthread.xm` 属于 RT-Thread 工程流程，裸机工程不需要依赖 RT-Thread 的构建产物。
