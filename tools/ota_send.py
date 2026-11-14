#!/usr/bin/env python3
"""通过串口把 .img 推给 STM32 OTA Bootloader。

依赖 pyserial：pip install pyserial

用法：
    python tools/ota_send.py --port COM3 --image build/app_b.img
    python tools/ota_send.py --port /dev/ttyUSB0 --image build/app_a.img --slot a

流程：HELLO -> BEGIN -> DATA xN -> END。
每个 DATA 帧都等 ACK，超时用同一序号重发（Bootloader 侧对重复序号是幂等的）。
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
import zlib
from pathlib import Path

try:
    import serial
except ImportError:  # pragma: no cover
    print("缺少 pyserial，请先执行：pip install pyserial", file=sys.stderr)
    raise SystemExit(2)

SOF = 0xA5
MAX_PAYLOAD = 1024
DATA_CHUNK = MAX_PAYLOAD - 4

CMD_HELLO = 0x01
CMD_BEGIN = 0x02
CMD_DATA = 0x03
CMD_END = 0x04
CMD_ABORT = 0x05
CMD_STATUS = 0x06

STATUS_TEXT = {
    0: "OK",
    1: "状态不允许",
    2: "参数非法",
    3: "Flash 操作失败",
    4: "整镜像 CRC 不符",
    5: "序号错乱",
    6: "数据量不符",
    7: "帧校验失败",
}

SLOT_A_BASE = 0x08003000
SLOT_B_BASE = 0x08009000


def build_frame(cmd: int, seq: int, payload: bytes = b"") -> bytes:
    head = bytes([SOF, cmd, seq]) + struct.pack("<H", len(payload))
    body = head + payload
    crc = zlib.crc32(body[1:]) & 0xFFFFFFFF
    return body + struct.pack("<I", crc)


def read_frame(ser: "serial.Serial", timeout: float = 3.0):
    """返回 (cmd, seq, payload)，超时返回 None。"""
    deadline = time.time() + timeout
    buf = bytearray()

    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buf += chunk

        while True:
            start = buf.find(SOF)
            if start < 0:
                buf.clear()
                break
            if start > 0:
                del buf[:start]
            if len(buf) < 5:
                break

            length = struct.unpack("<H", buf[3:5])[0]
            if length > MAX_PAYLOAD:
                del buf[:1]
                continue

            total = 5 + length + 4
            if len(buf) < total:
                break

            frame = bytes(buf[:total])
            crc = struct.unpack("<I", frame[-4:])[0]
            if (zlib.crc32(frame[1:-4]) & 0xFFFFFFFF) != crc:
                del buf[:1]
                continue

            del buf[:total]
            return frame[1], frame[2], frame[5 : 5 + length]

    return None


def transact(ser, cmd: int, seq: int, payload: bytes = b"", retries: int = 3):
    """发一帧并等响应，返回 (status, payload)。超时重试用同一 seq。"""
    for attempt in range(retries):
        ser.write(build_frame(cmd, seq, payload))
        ser.flush()

        # BEGIN 要擦 24 页 Flash，给足时间
        timeout = 5.0 if cmd == CMD_BEGIN else 2.0
        resp = read_frame(ser, timeout)
        if resp is None:
            print(f"    超时，重发 #{attempt + 1}", file=sys.stderr)
            continue

        rcmd, rseq, rpayload = resp
        if rcmd != cmd or rseq != seq:
            print(f"    响应不匹配（cmd={rcmd:#04x} seq={rseq}），忽略", file=sys.stderr)
            continue
        if not rpayload:
            print("    响应为空", file=sys.stderr)
            continue

        return rpayload[0], rpayload[1:]

    return -1, b""


def parse_image_header(data: bytes):
    if len(data) < 32:
        raise ValueError("文件比一个镜像头还短")
    magic, version, size, load_addr, entry, payload_crc, header_crc, flags = struct.unpack(
        "<IIIIIIII", data[:32]
    )
    if magic != 0x3141544F:
        raise ValueError(f"magic 不对（{magic:#010x}），不是 PortScope OTA 镜像")
    return {
        "version": version,
        "size": size,
        "load_addr": load_addr,
        "entry": entry,
        "payload_crc": payload_crc,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="推送 OTA 镜像到 STM32 Bootloader")
    ap.add_argument("--port", required=True, help="串口号，如 COM3 或 /dev/ttyUSB0")
    ap.add_argument("--image", required=True, help=".img 文件")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--slot", choices=["a", "b"], help="目标槽，默认按镜像头的 load_addr 推断")
    ap.add_argument("--chunk", type=int, default=DATA_CHUNK, help="每帧数据字节数")
    args = ap.parse_args()

    path = Path(args.image)
    if not path.is_file():
        print(f"找不到 {path}", file=sys.stderr)
        return 1

    data = path.read_bytes()
    try:
        info = parse_image_header(data)
    except ValueError as exc:
        print(f"镜像无效：{exc}", file=sys.stderr)
        return 1

    slot = 0 if args.slot == "a" else 1 if args.slot == "b" else None
    if slot is None:
        if info["load_addr"] == SLOT_A_BASE:
            slot = 0
        elif info["load_addr"] == SLOT_B_BASE:
            slot = 1
        else:
            print(f"无法推断槽位：load_addr={info['load_addr']:#010x}", file=sys.stderr)
            return 1

    chunk = max(64, min(args.chunk, DATA_CHUNK))
    total_crc = zlib.crc32(data) & 0xFFFFFFFF

    print(f"镜像   : {path.name}  {len(data)} 字节")
    print(f"版本   : {info['version'] >> 8}.{info['version'] & 0xFF}")
    print(f"目标槽 : {'B' if slot else 'A'}  ({info['load_addr']:#010x})")
    print(f"CRC32  : {total_crc:08X}")
    print()

    try:
        with serial.Serial(args.port, args.baud, timeout=0.05) as ser:
            time.sleep(0.2)
            ser.reset_input_buffer()

            print("[1/4] HELLO")
            status, payload = transact(ser, CMD_HELLO, 1)
            if status != 0:
                print(f"  握手失败：{STATUS_TEXT.get(status, status)}", file=sys.stderr)
                return 1
            if len(payload) >= 2:
                print(f"  Bootloader v{payload[0]}.{payload[1]}")

            print("[2/4] BEGIN")
            begin_payload = struct.pack("<BII", slot, len(data), total_crc)
            status, _ = transact(ser, CMD_BEGIN, 2)
            if status != 0:
                print(f"  开始失败：{STATUS_TEXT.get(status, status)}", file=sys.stderr)
                return 1
            print("  已擦除目标槽")

            print("[3/4] DATA")
            seq = 3
            offset = 0
            started = time.time()
            while offset < len(data):
                block = data[offset : offset + chunk]
                payload = struct.pack("<I", offset) + block
                status, _ = transact(ser, CMD_DATA, seq, payload)
                if status != 0:
                    print(
                        f"\n  写入失败 @{offset}：{STATUS_TEXT.get(status, status)}",
                        file=sys.stderr,
                    )
                    transact(ser, CMD_ABORT, seq + 1)
                    return 1

                offset += len(block)
                seq = (seq + 1) & 0xFF
                percent = offset * 100 // len(data)
                elapsed = time.time() - started
                rate = offset / elapsed / 1024 if elapsed > 0 else 0
                print(
                    f"\r  {offset}/{len(data)} 字节  {percent:3d}%  {rate:.1f} KB/s",
                    end="",
                    flush=True,
                )
            print()

            print("[4/4] END")
            status, _ = transact(ser, CMD_END, seq)
            if status != 0:
                print(f"  提交失败：{STATUS_TEXT.get(status, status)}", file=sys.stderr)
                return 1

            elapsed = time.time() - started
            print(f"\n完成：{len(data)} 字节，用时 {elapsed:.1f}s")
            print("设备将重启并试运行新固件；3 次内没有确认会自动回滚。")
            return 0

    except serial.SerialException as exc:
        print(f"串口错误：{exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\n已中断", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
