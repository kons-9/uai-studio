#!/usr/bin/env python3
"""Load and run a RAM-linked ELF through a pyOCD debug probe.

The STM32N6570-DK development image is linked into AXI SRAM rather than
non-volatile flash.  pyOCD's normal post-load reset would leave execution at
the board's boot address, so this helper loads the ELF, restores the initial
stack/program counter from its vector table, and resumes the core directly.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def _read_elf_range(elf, address: int, size: int) -> bytes:
    """Read bytes at a load address from PT_LOAD data in an ELF file."""
    for segment in elf.iter_segments():
        if segment.header.p_type != "PT_LOAD":
            continue
        start = segment.header.p_paddr
        end = start + segment.header.p_filesz
        if start <= address and address + size <= end:
            offset = address - start
            return segment.data()[offset : offset + size]
    raise ValueError(
        f"ELF does not contain {size} bytes at load address 0x{address:08x}"
    )


def _find_symbol(elf, name: str) -> int:
    symbol_table = elf.get_section_by_name(".symtab")
    if symbol_table is None:
        raise ValueError("ELF has no symbol table")
    for symbol in symbol_table.iter_symbols():
        if symbol.name == name:
            return int(symbol.entry.st_value)
    raise ValueError(f"ELF symbol not found: {name}")


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path, help="RAM-linked ELF image")
    parser.add_argument("--pack", required=True, type=Path, help="CMSIS-DAP DFP pack")
    parser.add_argument("--target", default="stm32n657x0hxq")
    parser.add_argument("--probe", default=None, help="Probe UID or partial UID")
    parser.add_argument("--frequency", default="2M", help="SWD frequency")
    return parser.parse_args()


def main() -> int:
    args = _parse_args()

    if not args.elf.is_file():
        print(f"error: ELF not found: {args.elf}", file=sys.stderr)
        return 2
    if not args.pack.is_file():
        print(f"error: CMSIS-DAP pack not found: {args.pack}", file=sys.stderr)
        return 2

    try:
        from elftools.elf.elffile import ELFFile
        from pyocd.core.helpers import ConnectHelper
        from pyocd.flash.file_programmer import FileProgrammer
    except ImportError as exc:
        print(
            "error: pyOCD is not installed in this Python environment; "
            "run .venv/bin/python -m pip install pyocd",
            file=sys.stderr,
        )
        print(f"detail: {exc}", file=sys.stderr)
        return 2

    try:
        with args.elf.open("rb") as image:
            elf = ELFFile(image)
            vector_address = _find_symbol(elf, "__Vectors")
            vector = _read_elf_range(elf, vector_address, 8)
            initial_sp, reset_handler = struct.unpack("<II", vector)
    except (OSError, ValueError, struct.error) as exc:
        print(f"error: unable to inspect ELF vector table: {exc}", file=sys.stderr)
        return 2

    options = {
        "target_override": args.target,
        "pack": str(args.pack),
        "frequency": args.frequency,
    }
    session = ConnectHelper.session_with_chosen_probe(
        blocking=False,
        return_first=True,
        unique_id=args.probe,
        options=options,
    )
    if session is None:
        print("error: no matching debug probe is connected", file=sys.stderr)
        return 3

    with session:
        target = session.board.target
        target.halt()
        print(f"Loading {args.elf} at RAM vector 0x{vector_address:08x}...")
        FileProgrammer(session).program(str(args.elf))

        # The image is RAM-resident. Do not reset: a reset would return to the
        # board's boot flow instead of the freshly loaded development image.
        target.write_core_register("sp", initial_sp)
        target.write_core_register("pc", reset_handler & ~1)
        print(
            f"Running at 0x{reset_handler & ~1:08x} "
            f"with SP=0x{initial_sp:08x}"
        )
        target.resume()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
