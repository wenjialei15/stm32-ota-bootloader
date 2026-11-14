#!/usr/bin/env python3
"""给裸 bin 加 32 字节镜像头，产出可 OTA 推送的 .img。

头的定义在 shared/image.h，字段顺序和大小必须与 C 结构体逐字节一致。
这里刻意用 struct 显式指定小端，而不是依赖平台字节序。

用法：
    python tools/ota_pack.py build/app_a.bin --load-addr 0x08003000 -o build/app_a.img
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

MAGIC = 0x3141544F  # "OTA1" 小端
HEADER_SIZE = 32

# 与 image.h 中的 flash 分区保持一致
SLOT_A_BASE = 0x08003000
SLOT_B_BASE = 0x08009000
SLOT_SIZE = 24 * 1024


def build_header(payload: bytes, load_addr: int, version: int, flags: int = 0) -> bytes:
    image_size = len(payload)
    entry = load_addr + HEADER_SIZE + 4
    payload_crc = zlib.crc32(payload) & 0xFFFFFFFF

    # header_crc 覆盖它前面的 24 字节
    head24 = struct.pack(
        "<IIIIII", MAGIC, version, image_size, load_addr, entry, payload_crc
    )
    header_crc = zlib.crc32(head24) & 0xFFFFFFFF
    return head24 + struct.pack("<II", header_crc, flags)


def main() -> int:
    parser = argparse.ArgumentParser(description="为 STM32 OTA 镜像添加头部")
    parser.add_argument("input", help="裸固件 bin")
    parser.add_argument("--load-addr", required=True, help="目标槽地址，如 0x08003000")
    parser.add_argument("--version", default="0x000100", help="版本号，默认 0x000100")
    parser.add_argument("-o", "--output", help="输出文件，默认同名 .img")
    args = parser.parse_args()

    src = Path(args.input)
    if not src.is_file():
        print(f"错误：找不到 {src}", file=sys.stderr)
        return 1

    payload = src.read_bytes()
    if not payload:
        print("错误：固件是空的", file=sys.stderr)
        return 1

    load_addr = int(args.load_addr, 0)
    version = int(args.version, 0)

    if load_addr not in (SLOT_A_BASE, SLOT_B_BASE):
        print(
            f"错误：load_addr {load_addr:#010x} 不是已知槽地址"
            f"（A={SLOT_A_BASE:#010x} B={SLOT_B_BASE:#010x}）",
            file=sys.stderr,
        )
        return 1

    total = HEADER_SIZE + len(payload)
    if total > SLOT_SIZE:
        print(
            f"错误：镜像 {total} 字节超出槽容量 {SLOT_SIZE} 字节，"
            f"超出 {total - SLOT_SIZE}",
            file=sys.stderr,
        )
        return 1

    header = build_header(payload, load_addr, version)
    image = header + payload

    out = Path(args.output) if args.output else src.with_suffix(".img")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(image)

    usage = total * 100 // SLOT_SIZE
    print(f"镜像  : {out}")
    print(f"槽    : {load_addr:#010x} ({'A' if load_addr == SLOT_A_BASE else 'B'})")
    print(f"负载  : {len(payload)} 字节  crc32={zlib.crc32(payload) & 0xFFFFFFFF:08X}")
    print(f"总计  : {total}/{SLOT_SIZE} 字节  占用 {usage}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
