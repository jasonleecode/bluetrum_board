# AB32VG1 骄龙开发板 BSP 说明

## 简介

主要内容如下：

- 开发板资源介绍
- BSP 快速上手
- 进阶使用方法

通过阅读快速上手章节开发者可以快速地上手该 BSP，将 RT-Thread 运行在开发板上。在进阶使用指南章节，将会介绍更多高级功能，帮助开发者利用 RT-Thread 驱动更多板载资源。

## 注意事项

芯片有部分不开源的代码是以静态库提供的，静态库在软件包中，默认已勾选，直接运行 `pkgs --update` 即可

波特率默认为 1.5M，需要使用 [Downloader](https://github.com/BLUETRUM/Downloader) 下载 `.dcf` 到芯片，需要编译后自动下载，需要在 `Downloader` 中的下载的下拉窗中选择 `自动`；目前暂时屏蔽 uart1 打印

使用 `romfs` 时，需要自己生成 `romfs.c` 进行替换，操作参考[使用 RomFS](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/tutorial/qemu-network/filesystems/filesystems?id=%e4%bd%bf%e7%94%a8-romfs)

编译报错的时候，如果出现重复定义的报错，可能需要在 `cconfig.h` 中手动添加以下配置

``` c
#define HAVE_SIGEVENT 1
#define HAVE_SIGINFO 1
#define HAVE_SIGVAL 1
```

所有在中断中使用的函数或数据需要放在 RAM 中，否则会导致系统运行报错。具体做法可以参考下面

``` c
rt_section(".irq.example.str")
static const char example_info[] = "example 0x%x";

rt_section(".irq.example")
void example_isr(void)
{
    rt_kprintf(example_info, 11);
    ...
}
```



## 使用说明

使用说明分为如下两个章节：

- 快速上手

    本章节是为刚接触 RT-Thread 的新手准备的使用说明，遵循简单的步骤即可将 RT-Thread 操作系统运行在该开发板上，看到实验效果 。

- 进阶使用

    本章节是为需要在 RT-Thread 操作系统上使用更多开发板资源的开发者准备的。通过使用 ENV 工具对 BSP 进行配置，可以开启更多板载资源，实现更多高级功能。


### 快速上手

本 BSP 为开发者提供 GCC 开发环境。下面介绍如何将系统运行起来。

#### 硬件连接

使用数据线连接开发板到 PC，打开电源开关。

#### 编译下载

通过 `RT-Thread Studio` 或者 `scons` 编译得到 `.dcf` 固件，通过 `Downloader` 进行下载

#### 运行结果

下载程序成功之后，系统会自动运行，观察开发板上 LED 的运行效果，红色 LED 会周期性闪烁。

连接开发板对应串口到 PC , 在终端工具里打开相应的串口（115200-8-1-N），复位设备后，可以看到 RT-Thread 的输出信息:

```bash
 \ | /
- RT -     Thread Operating System
 / | \     4.0.3 build Dec  9 2020
 2006 - 2020 Copyright by rt-thread team
msh >
```
### 进阶使用

此 BSP 默认只开启了 GPIO 和 串口0 的功能，如果需使用 SD 卡、Flash 等更多高级功能，需要利用 ENV 工具对BSP 进行配置，步骤如下：

1. 在 bsp 下打开 env 工具。

2. 输入`menuconfig`命令配置工程，配置好之后保存退出。

3. 输入`pkgs --update`命令更新软件包。

4. 输入`scons` 命令重新编译工程。

## 维护人信息

- [greedyhao](https://github.com/greedyhao)
