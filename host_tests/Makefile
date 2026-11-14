# 主机侧单元测试：不需要任何嵌入式工具链，普通 cc 即可。
# 覆盖 CRC、镜像校验、元数据掉电安全、启动决策四个纯逻辑模块。

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Werror
INCLUDES = -I../shared -I../bootloader/src -I.

SRCS = tests_main.c \
       test_support.c \
       test_crc.c \
       test_image.c \
       test_metadata.c \
       test_boot_decision.c \
       ../shared/crc32.c \
       ../bootloader/src/image.c \
       ../bootloader/src/metadata.c \
       ../bootloader/src/boot_decision.c

OUT = build/host_tests

.PHONY: all run clean

all: run

$(OUT): $(SRCS) test_support.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SRCS)

run: $(OUT)
	@./$(OUT)

clean:
	@rm -rf build
