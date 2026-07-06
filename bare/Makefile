################################################################################
# AB32VG1 Makefile (Multi-Target)
################################################################################

# 1. 编译工具链配置 (根据你的环境修改路径)
CROSS_COMPILE ?= riscv64-unknown-elf-
BINUTILS_PREFIX ?= $(CROSS_COMPILE)
# 或者使用: riscv64-unknown-elf- 
CC      := $(CROSS_COMPILE)gcc
OBJCOPY := $(BINUTILS_PREFIX)objcopy
SIZE    := $(BINUTILS_PREFIX)size
AR      := $(BINUTILS_PREFIX)ar

# 2. 目录定义
BUILD_DIR := build
APP_DIR   := app
DRV_DIR   := drivers
LIB_DIR   := lib

# 3. 目标定义 (根据 app 目录下的文件名，定义三个独立的目标)
# 这里的逻辑是：取出 app/*.c 的文件名，去掉后缀，作为最终生成文件的名字
APP_SRCS := $(wildcard $(APP_DIR)/*.c)
TARGETS  := $(patsubst $(APP_DIR)/%.c, %, $(APP_SRCS))

# 4. 核心编译参数 (RISC-V 架构参数)
# AB32VG1 通常是 RV32IMAC，具体请参考芯片手册
ARCH_FLAGS := -march=rv32imac -mabi=ilp32
OPT_FLAGS  := -Os -g

# C 编译选项
CFLAGS := $(ARCH_FLAGS) $(OPT_FLAGS) \
	-ffreestanding \
    -Wall -ffunction-sections -fdata-sections \
    -I$(DRV_DIR)/inc \
    -I$(APP_DIR) \
    -MMD -MP # 生成依赖文件

# 汇编选项
ASFLAGS := $(ARCH_FLAGS) -x assembler-with-cpp

# 链接选项
LDSCRIPT := link.ld
LDLIBS   := -lhal -lbtctrl -lgcc
LDFLAGS  := $(ARCH_FLAGS) $(OPT_FLAGS) \
    -T$(LDSCRIPT) \
    -Wl,--gc-sections \
    -Wl,--print-memory-usage \
    -nostartfiles \
    -nostdlib \
    -L$(LIB_DIR) \
    $(LDLIBS)  # 链接 vendor 库和 libgcc

# 5. 源文件收集
# 共享的驱动源文件
DRV_SRCS := $(wildcard $(DRV_DIR)/src/*.c)
# 启动文件
ASM_SRCS := startup.S

# 6. 生成对应的 Object 文件路径
# 驱动 .o
DRV_OBJS := $(addprefix $(BUILD_DIR)/, $(DRV_SRCS:.c=.o))
# 启动 .o
ASM_OBJS := $(addprefix $(BUILD_DIR)/, $(ASM_SRCS:.S=.o))

# 所有的共享 Object (驱动 + 启动)
COMMON_OBJS := $(DRV_OBJS) $(ASM_OBJS)

################################################################################
# 构建规则
################################################################################

# 默认目标：构建所有 APP
all: $(addprefix $(BUILD_DIR)/, $(addsuffix .elf, $(TARGETS)))

# 打印检测到的目标 (调试用)
info:
	@echo "Detected Targets: $(TARGETS)"
	@echo "Common Objs: $(COMMON_OBJS)"

# --- 链接规则 (针对每个独立的 APP) ---
# 依赖关系：具体的 APP .o + 公共驱动 .o + 公共启动 .o
$(BUILD_DIR)/%.elf: $(BUILD_DIR)/$(APP_DIR)/%.o $(COMMON_OBJS)
	@echo "------------------------------------------------"
	@echo "Linking Target: $@"
	@mkdir -p $(dir $@)
	$(CC) $< $(COMMON_OBJS) $(LDFLAGS) -o $@
	$(OBJCOPY) -O binary $@ $(@:.elf=.bin)
	@echo "Generated: $(@:.elf=.bin)"
	$(SIZE) $@

# --- 编译规则 ---

# 编译 APP 目录下的 .c
$(BUILD_DIR)/$(APP_DIR)/%.o: $(APP_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling APP: $<"
	$(CC) -c $(CFLAGS) $< -o $@

# 编译 Drivers 目录下的 .c
$(BUILD_DIR)/$(DRV_DIR)/src/%.o: $(DRV_DIR)/src/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling Driver: $<"
	$(CC) -c $(CFLAGS) $< -o $@

# 编译 启动文件 .S
$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "Assembling: $<"
	$(CC) -c $(ASFLAGS) $< -o $@

# 清理规则
clean:
	rm -rf $(BUILD_DIR)

# 引入依赖文件 (.d)
-include $(wildcard $(BUILD_DIR)/**/*.d)

.PHONY: all clean info
