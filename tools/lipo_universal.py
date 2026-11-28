#!/usr/bin/env python3
"""Combine 64-bit Mach-O slices (e.g. x86_64 + arm64) into a universal binary."""
import struct
import sys
from pathlib import Path

MH_MAGIC_64 = 0xFEEDFACF
FAT_MAGIC = 0xCAFEBABE
PAGE_ALIGN_POW2 = 14  # 16 KiB alignment required by Apple Silicon macOS
PAGE_SIZE = 1 << PAGE_ALIGN_POW2


def main() -> int:
    if len(sys.argv) < 4:
        print(f"usage: {sys.argv[0]} <output> <slice1> <slice2> [...]", file=sys.stderr)
        return 2
    out_path = Path(sys.argv[1])
    slices = []
    seen_cputypes = set()
    for arg in sys.argv[2:]:
        data = Path(arg).read_bytes()
        if len(data) < 32:
            raise ValueError(f"{arg}: file too small to be a Mach-O image")
        magic, cputype, cpusubtype = struct.unpack("<III", data[:12])
        if magic != MH_MAGIC_64:
            raise ValueError(f"{arg}: expected 64-bit Mach-O (0xFEEDFACF), got {magic:#x}")
        if cputype in seen_cputypes:
            raise ValueError(f"{arg}: duplicate cputype {cputype:#x}")
        seen_cputypes.add(cputype)
        slices.append((cputype, cpusubtype, data))

    offset = PAGE_SIZE
    header = bytearray(struct.pack(">II", FAT_MAGIC, len(slices)))
    placements = []
    for cputype, cpusubtype, data in slices:
        header.extend(struct.pack(">IIIII", cputype, cpusubtype, offset, len(data), PAGE_ALIGN_POW2))
        placements.append((offset, data))
        offset = ((offset + len(data) + PAGE_SIZE - 1) // PAGE_SIZE) * PAGE_SIZE

    image = bytearray(header)
    for slice_offset, data in placements:
        image.extend(b"\0" * (slice_offset - len(image)))
        image.extend(data)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(image)
    out_path.chmod(0o755)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
