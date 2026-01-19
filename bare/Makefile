# ======================
# Toolchain
# ======================
CROSS_COMPILE ?= riscv-none-embed-
CC      := $(CROSS_COMPILE)gcc
AS      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)gcc
AR      := $(CROSS_COMPILE)ar
OBJDUMP := $(CROSS_COMPILE)objdump
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE    := $(CROSS_COMPILE)size

# ======================
# Target
# ======================
TARGET := app
BUILD  := build

# ======================
# Paths
# ======================
HAL_LIB := lib/libhal.a

INC_DIRS := \
    drivers/include \
    src

SRC_DIRS := \
    src \
    drivers/src \
    startup

# ======================
# Files
# ======================
C_SRCS := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
S_SRCS := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.S))

OBJS := \
    $(C_SRCS:%.c=$(BUILD)/%.o) \
    $(S_SRCS:%.S=$(BUILD)/%.o)

DEPS := $(OBJS:.o=.d)

# ======================
# Flags
# ======================
CFLAGS := -Os -g -ffunction-sections -fdata-sections
CFLAGS += $(addprefix -I,$(INC_DIRS))
CFLAGS += -Wall -Wextra

ASFLAGS := $(CFLAGS)

LDFLAGS := \
    -T linker.ld \
    -Wl,--gc-sections \
    -Wl,-Map=$(BUILD)/$(TARGET).map

# ======================
# Default target
# ======================
all: $(BUILD)/$(TARGET).elf

# ======================
# Build rules
# ======================
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

# ======================
# Link
# ======================
$(BUILD)/$(TARGET).elf: $(OBJS)
	$(LD) $(OBJS) \
	    $(HAL_LIB) \
	    $(LDFLAGS) \
	    -o $@
	$(SIZE) $@

# ======================
# Binary / Hex
# ======================
bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $(BUILD)/$(TARGET).bin

hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $(BUILD)/$(TARGET).hex

# ======================
# Clean
# ======================
clean:
	rm -rf $(BUILD)

# ======================
# Dependencies
# ======================
-include $(DEPS)