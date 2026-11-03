# Build from the original APP directory, with only this variant's C files.
# The caller creates OBJDIR/obj_files; no rm, mkdir, board or Flash recipes.
PROJ_NAME ?= neonFxGardenDemo
OBJDIR ?= variants/neon_fx_20261013/build
VARIANT_SRC ?= variants/neon_fx_20261013/src
STANDALONE = ../..
SRCS ?= $(wildcard $(VARIANT_SRC)/*.c) $(STANDALONE)/common/start.S $(STANDALONE)/common/trap.S

include $(STANDALONE)/common/bsp.mk
include $(STANDALONE)/common/riscv64-unknown-elf.mk

CFLAGS += -std=gnu99 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter
CFLAGS += -Wno-comment -Wno-unused-but-set-variable $(CFLAGS_ARGS)
CFLAGS += -I$(VARIANT_SRC) -I$(STANDALONE)/include -I$(STANDALONE)/driver
CFLAGS += -ffunction-sections -fdata-sections -flto
LDFLAGS += -lc -L$(STANDALONE)/common -specs=nosys.specs -lgcc
LDFLAGS += -nostartfiles -ffreestanding -Wl,-Bstatic,-T,$(LDSCRIPT),-Map,$(OBJDIR)/$(PROJ_NAME).map,--print-memory-usage,--no-warn-rwx-segment,--gc-sections -lm

BUILD_SRCS := $(SRCS) $(STANDALONE)/common/syscalls.c
OBJS := $(addprefix $(OBJDIR)/obj_files/,$(notdir $(BUILD_SRCS)))
OBJS := $(OBJS:.c=.o)
OBJS := $(OBJS:.S=.o)

all: $(OBJDIR)/$(PROJ_NAME).elf $(OBJDIR)/$(PROJ_NAME).hex $(OBJDIR)/$(PROJ_NAME).asm $(OBJDIR)/$(PROJ_NAME).bin

$(OBJDIR)/%.elf: $(OBJS)
	@echo LD $(PROJ_NAME)
	@$(RISCV_CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.hex: %.elf
	@$(RISCV_OBJCOPY) -O ihex $^ $@

%.bin: %.elf
	@$(RISCV_OBJCOPY) -O binary $^ $@

%.asm: %.elf
	@$(RISCV_OBJDUMP) -S -d $^ > $@

define COMPILE_SOURCE
$(OBJDIR)/obj_files/$(notdir $(basename $(1))).o: $(1)
	@echo CC $(1)
	@$(RISCV_CC) -c $(CFLAGS) -o $$@ $$<
endef
$(foreach source,$(BUILD_SRCS),$(eval $(call COMPILE_SOURCE,$(source))))

$(OBJS): $(wildcard $(VARIANT_SRC)/*.h)
.SECONDARY: $(OBJS)
