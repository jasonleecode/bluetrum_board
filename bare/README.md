这是基于ab32vg1芯片的裸机程序，由于ab32vg1提供的驱动是闭源的，在分析官方提供的libhal.a文件后，得到大致的驱动信息，并进行了简单的封装。

程序编译完成后，需要使用riscv32-elf-xmaker工具将用户程序生成的bin文件和厂家提供的header.bin合成最后的用于download的文件。
riscv32-elf-xmaker -b rtthread.xm
riscv32-elf-xmaker -b download.xm