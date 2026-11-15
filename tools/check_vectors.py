#!/usr/bin/env python3
"""Verify actual SDCC-linked CH543 interrupt vectors in Intel HEX + link map.

Usage: python3 tools/check_vectors.py build/ch543-dp-source.ihx [--map path]
Checks image bytes, not merely declarations or assembler text. No hardware or
timing validation is implied. Addresses follow CH543DS1 / CH543.H INT_ADDR_*.
"""
import argparse
from pathlib import Path
import re


VECTORS = (
    (0x00, "Reset", "__sdcc_gsinit_startup"),
    (0x03, "INT0", "_hpd_edge_isr"),
    (0x0B, "Timer0", "_timer0_isr"),
    (0x1B, "Timer1", "_timer1_isr"),
    (0x2B, "Timer2", "_timer2_isr"),
    (0x53, "USBPD", "_pd_isr"),
)


def read_hex(path):
    memory = {}
    base = 0
    eof = False
    for lineno, line in enumerate(path.read_text().splitlines(), 1):
        if not line:
            continue
        if eof or not line.startswith(":"):
            raise ValueError(f"{path}:{lineno}: invalid record or data after EOF")
        record = bytes.fromhex(line[1:])
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 255:
            raise ValueError(f"{path}:{lineno}: invalid record length/checksum")
        count, address, kind = record[0], int.from_bytes(record[1:3], "big"), record[3]
        data = record[4:-1]
        if kind == 0:
            for offset, byte in enumerate(data):
                target = base + address + offset
                if target in memory and memory[target] != byte:
                    raise ValueError(f"conflicting image bytes at 0x{target:04x}")
                memory[target] = byte
        elif kind == 1:
            if count != 0 or address != 0:
                raise ValueError("malformed EOF")
            eof = True
        elif kind in (2, 4):
            if count != 2 or address != 0:
                raise ValueError("malformed extended address")
            base = int.from_bytes(data, "big") << (4 if kind == 2 else 16)
        elif kind not in (3, 5):
            raise ValueError(f"unsupported Intel HEX record type {kind}")
    if not eof:
        raise ValueError("missing Intel HEX EOF record")
    return memory


def read_symbols(path):
    symbols = {}
    for address, name in re.findall(
        r"^\s*C:\s*([0-9A-Fa-f]+)\s+(\S+)", path.read_text(), re.MULTILINE
    ):
        symbols[name] = int(address, 16)
    return symbols


def verify(image, map_file, board2=False, diagnostic=False):
    memory = read_hex(image)
    symbols = read_symbols(map_file)
    checked = []
    for address, name, symbol in VECTORS:
        if diagnostic and name != "Reset":
            continue  # Standalone diagnostic has every interrupt disabled.
        if board2 and name == "INT0":
            continue  # Board2 uses Timer2 CAP1 on P1.4 for HPD.
        if symbol not in symbols:
            raise ValueError(f"{name}: missing map symbol {symbol}")
        instruction = bytes(memory.get(address + n, 0xFF) for n in range(3))
        if instruction[0] != 0x02:
            raise ValueError(
                f"{name} at 0x{address:04x}: expected LJMP (02), "
                f"got {instruction.hex(' ')}; ISR declarations may be missing in main.c"
            )
        actual = int.from_bytes(instruction[1:], "big")
        expected = symbols[symbol]
        if actual != expected:
            raise ValueError(
                f"{name}: vector jumps to 0x{actual:04x}, "
                f"but {symbol} is at 0x{expected:04x}"
            )
        if actual not in memory or not (0 <= actual < 0x3800):
            raise ValueError(f"{name}: handler outside populated application image")
        checked.append(f"{name}@{address:04x}->{actual:04x}")
    # Verify the actual runtime calls our early hook, not a similarly named
    # unused function. SDCC 4.3 changed the C hook spelling; older lib versions
    # must still resolve to this project's implementation in module main.
    hooks = re.findall(
        r"^\s*C:\s*([0-9A-Fa-f]+)\s+_{2,3}sdcc_external_startup\s+main\s*$",
        map_file.read_text(), re.MULTILINE,
    )
    if len(hooks) != 1 or "s_GSINIT2" not in symbols:
        raise ValueError("missing unique project-owned early startup hook/GSINIT2")
    hook = int(hooks[0], 16)
    init = symbols["s_GSINIT2"]
    call = bytes(memory.get(init + n, 0xFF) for n in range(3))
    if call[0] != 0x12 or int.from_bytes(call[1:], "big") != hook:
        raise ValueError("C runtime does not LCALL the project's early startup hook")
    # MOV IE,#0; MOV IE_EX,#0; MOV XBUS_AUX,#0 must precede RAM initialization.
    hook_bytes = bytes(memory.get(hook + n, 0xFF) for n in range(9))
    if hook_bytes != bytes.fromhex("75 a8 00 75 e8 00 75 a2 00"):
        raise ValueError("startup hook does not clear IE/IE_EX/DPS/autoincrement as audited")
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--map", dest="map_file", type=Path)
    parser.add_argument("--board2", action="store_true")
    parser.add_argument("--diagnostic", action="store_true")
    args = parser.parse_args()
    try:
        checks = verify(args.image, args.map_file or args.image.with_suffix(".map"), args.board2, args.diagnostic)
    except (ValueError, OSError) as exc:
        parser.exit(1, f"vector verification FAILED: {exc}\n")
    print(f"{args.image}: all {len(checks)} vectors + early startup verified: {', '.join(checks)}")


if __name__ == "__main__":
    main()
