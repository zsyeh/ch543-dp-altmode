#!/usr/bin/env python3
"""Check a BL51 map and OH51 Intel HEX for the CH543 application."""

import argparse
from pathlib import Path
import re

from check_vectors import VECTORS, read_hex


def verify(image, map_file, board2):
    memory = read_hex(image)
    if not memory or max(memory) >= 0x3800:
        raise ValueError("application HEX is empty or extends past 0x37FF")
    segments = {
        name: (kind, int(base, 16), int(size, 16))
        for kind, base, size, name in re.findall(
            r"^\s*(CODE|IDATA|XDATA)\s+([0-9A-F]+)H\s+([0-9A-F]+)H\s+UNIT\s+(\S+)",
            map_file.read_text(), re.MULTILINE,
        )
    }
    stack_kind, stack, _ = segments["?STACK"]
    if stack_kind != "IDATA" or not 0 < stack < 0x100:
        raise ValueError("stack starts outside CH543 iRAM")
    if any(base + size > 0x100 for kind, base, size in segments.values()
           if kind == "XDATA"):
        raise ValueError("XDATA extends beyond CH543 on-chip xRAM")
    if segments["?XD?CH543_PHY"] != ("XDATA", 0, 130):
        raise ValueError("unexpected layout for the PHY DMA and GoodCRC buffers")

    def jump(address):
        opcode, high, low = (memory.get(address + offset, 0xff) for offset in range(3))
        target = (high << 8) | low
        if opcode != 0x02 or target not in memory:
            raise ValueError(f"invalid LJMP at 0x{address:04X}")
        return target

    checked = []
    for address, irq, symbol in VECTORS:
        if board2 and irq == "INT0":
            continue
        if irq == "Reset":
            segment = "?C_C51STARTUP"
        else:
            module = "CH543_PHY" if irq in ("Timer0", "USBPD") else (
                "CH543_BOARD2" if board2 else "CH543_BOARD"
            )
            segment = f"?PR?{symbol[1:].upper()}?{module}"
        _, target, _ = segments[segment]
        if jump(address) != target:
            raise ValueError(f"{irq} vector does not jump to {segment}")
        checked.append(f"{irq}@{address:04X}->{target:04X}")

    _, startup, _ = segments["?C_C51STARTUP"]
    # IE/IE_EX/DPTR/PSW clear, iRAM/xRAM clear loops, then SP initialization.
    prefix = bytes.fromhex("75 A8 00 75 E8 00 75 A2 00 75 D0 00 E4 78 FF F6 D8 FD"
                           " 90 00 00 F0 A3 D8 FC 75 81") + bytes([stack - 1])
    if any(memory.get(startup + offset) != byte for offset, byte in enumerate(prefix)):
        raise ValueError("reset code does not match CH543 RAM and stack initialization")
    jump(startup + len(prefix))
    return checked, 0x100 - stack


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("map_file", type=Path)
    parser.add_argument("--board2", action="store_true")
    args = parser.parse_args()
    try:
        checks, capacity = verify(args.image, args.map_file, args.board2)
    except (KeyError, OSError, ValueError) as exc:
        parser.exit(1, f"Keil image verification FAILED: {exc}\n")
    print(f"{', '.join(checks)}; startup verified; stack capacity {capacity} B")


if __name__ == "__main__":
    main()
