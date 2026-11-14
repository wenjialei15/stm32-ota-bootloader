#!/usr/bin/env python3
"""独立校验 .img 镜像。

刻意不复用 ota_pack.py 的 build_header——两套实现各自按文档写一遍，
能交叉验证 C 侧与 Python 侧对格式的理解是否一致。

用法：
    python tools/ota_verify.py build/app_a.img [more.img ...]
"""

from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

MAGIC = 0x3141544F
HEADER_SIZE = 32
SLOT_A_BASE = 0x08003000
SLOT_B_BASE = 0x08009000
SLOT_SIZE = 24 * 1024


def verify(path: Path) -> tuple[bool, str]:
    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        return False, "文件比一个头还短"

    magic, version, size, load_addr, entry, payload_crc, header_crc, flags = struct.unpack(
        "<IIIIIIII", data[:HEADER_SIZE]
    )

    if magic != MAGIC:
        return False, f"magic 不对：{magic:#010x}"

    calc_head = zlib.crc32(data[:24]) & 0xFFFFFFFF
    if calc_head != header_crc:
        return False, f"header crc 不对：算出 {calc_head:08X}，头里写 {header_crc:08X}"

    if load_addr not in (SLOT_A_BASE, SLOT_B_BASE):
        return False, f"load_addr {load_addr:#010x} 不是已知槽"

    if size == 0 or size > SLOT_SIZE - HEADER_SIZE:
        return False, f"负载长度 {size} 非法"

    if len(data) - HEADER_SIZE != size:
        return False, f"实际负载 {len(data) - HEADER_SIZE} 与头部声明 {size} 不符"

    if entry != load_addr + HEADER_SIZE + 4:
        return False, f"入口地址 {entry:#010x} 与约定不符"

    calc_payload = zlib.crc32(data[HEADER_SIZE:]) & 0xFFFFFFFF
    if calc_payload != payload_crc:
        return False, f"payload crc 不对：算出 {calc_payload:08X}，头里写 {payload_crc:08X}"

    # 复位向量必须落在自己这个槽里，否则跳过去必炸
    if not (load_addr + HEADER_SIZE <= entry < load_addr + SLOT_SIZE):
        return False, "复位向量超出槽范围"

    slot = "A" if load_addr == SLOT_A_BASE else "B"
    return True, f"OK  槽{slot}  v{version >> 8}.{version & 0xFF}  {size} 字节  CRC {payload_crc:08X}"


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    failed = 0
    for arg in sys.argv[1:]:
        path = Path(arg)
        if not path.is_file():
            print(f"FAIL {arg}: 文件不存在")
            failed += 1
            continue
        ok, message = verify(path)
        print(f"{'PASS' if ok else 'FAIL'} {path.name}: {message}")
        if not ok:
            failed += 1

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
