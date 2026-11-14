# STM32F103C8T6 OTA Bootloader
#
# 目标：build/bootloader.bin、build/app_a.bin、build/app_b.bin
# 主机测试：make host-test（不需要 ARM 工具链）

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size

BUILD := build

MCU      := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
CFLAGS   := $(MCU) -std=gnu11 -O2 -g3 -Wall -Wextra -Wshadow \
            -ffunction-sections -fdata-sections \
            -I shared -I bootloader/inc -I bootloader/src
LDFLAGS  := $(MCU) -nostartfiles -Wl,--gc-sections -Wl,--print-memory-usage

BOOT_SRC := shared/startup_stm32f103.c \
            shared/system.c \
            shared/crc32.c \
            shared/flash_storage.c \
            bootloader/src/main.c \
            bootloader/src/boot_decision.c \
            bootloader/src/metadata.c \
            bootloader/src/image.c \
            bootloader/src/flash_if.c \
            bootloader/src/uart.c \
            bootloader/src/ota_proto.c

# 应用也要写元数据（启动确认），所以同样带上了 flash_if 和 metadata
APP_SRC  := shared/startup_stm32f103.c \
            shared/system.c \
            shared/crc32.c \
            shared/flash_storage.c \
            bootloader/src/uart.c \
            bootloader/src/flash_if.c \
            bootloader/src/metadata.c \
            app/src/main.c

# 槽 A 和槽 B 必须各编一份：代码是绝对地址链接，同一份 bin 只能跑在自己的槽里。
SLOT_A_ADDR := 0x08003000
SLOT_B_ADDR := 0x08009000

.PHONY: all clean flash images host-test size help crash

all: $(BUILD)/bootloader.bin $(BUILD)/app_a.bin $(BUILD)/app_b.bin $(BUILD)/app_crash.bin

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/bootloader.elf: $(BOOT_SRC) bootloader/linker/bootloader.ld | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -T bootloader/linker/bootloader.ld \
		-Wl,-Map=$(BUILD)/bootloader.map -o $@ $(BOOT_SRC)

$(BUILD)/bootloader.bin: $(BUILD)/bootloader.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/app_a.elf: $(APP_SRC) app/linker/app.ld | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -T app/linker/app.ld \
		-Wl,--defsym=SLOT_BASE=$(SLOT_A_ADDR) \
		-Wl,-Map=$(BUILD)/app_a.map -o $@ $(APP_SRC)

$(BUILD)/app_b.elf: $(APP_SRC) app/linker/app.ld | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -T app/linker/app.ld \
		-Wl,--defsym=SLOT_BASE=$(SLOT_B_ADDR) \
		-Wl,-Map=$(BUILD)/app_b.map -o $@ $(APP_SRC)

$(BUILD)/app_a.bin: $(BUILD)/app_a.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/app_b.bin: $(BUILD)/app_b.elf
	$(OBJCOPY) -O binary $< $@

# 演示用的"坏固件"：镜像完整、CRC 正确，但启动后立刻 HardFault。
# 用来验证回滚——这是唯一能证明 retry 计数真的生效的办法。
$(BUILD)/app_crash.elf: $(APP_SRC) app/linker/app.ld | $(BUILD)
	$(CC) $(CFLAGS) -DDEMO_CRASH=1 $(LDFLAGS) -T app/linker/app.ld \
		-Wl,--defsym=SLOT_BASE=$(SLOT_B_ADDR) \
		-Wl,-Map=$(BUILD)/app_crash.map -o $@ $(APP_SRC)

$(BUILD)/app_crash.bin: $(BUILD)/app_crash.elf
	$(OBJCOPY) -O binary $< $@

# 给裸 bin 加上 32 字节镜像头，得到可 OTA 推送的 .img
images: all
	python tools/ota_pack.py $(BUILD)/app_a.bin --load-addr $(SLOT_A_ADDR) -o $(BUILD)/app_a.img
	python tools/ota_pack.py $(BUILD)/app_b.bin --load-addr $(SLOT_B_ADDR) -o $(BUILD)/app_b.img
	python tools/ota_pack.py $(BUILD)/app_crash.bin --load-addr $(SLOT_B_ADDR) -o $(BUILD)/app_crash.img

size: all
	$(SIZE) $(BUILD)/bootloader.elf $(BUILD)/app_a.elf $(BUILD)/app_b.elf $(BUILD)/app_crash.elf

flash: $(BUILD)/bootloader.bin
	st-flash write $(BUILD)/bootloader.bin 0x08000000

# 首次烧录应用必须写 .img 而不是 .bin：
# .bin 没有 32 字节镜像头，Bootloader 校验会直接判为非法镜像。
flash-app: images
	st-flash write $(BUILD)/app_a.img $(SLOT_A_ADDR)

host-test:
	$(MAKE) -C host_tests

clean:
	@rm -rf $(BUILD) host_tests/build

help:
	@echo "make            编译 bootloader 和三个 app（含演示用坏固件）"
	@echo "make images     生成带镜像头的 .img（OTA 推送用）"
	@echo "make size       查看各镜像占用"
	@echo "make host-test  跑主机侧单元测试（不需要 ARM 工具链）"
	@echo "make flash      用 st-flash 烧录 bootloader"
	@echo "make flash-app  烧录 A 槽应用到 0x08003000（写 .img 不是 .bin）"
