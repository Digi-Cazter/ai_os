"""Measure payload, reserved RAM, and artifacts separately; no guessed totals."""
import sys
from pathlib import Path
from elf import read_elf

_, _, segments = read_elf(sys.argv[1])
loaded = [s for s in segments if s[2]]
payload = sum(len(data) for _, data, _, _ in loaded)
reserved = sum(memory for _, _, memory, _ in loaded)
span = max(addr + memory for addr, _, memory, _ in loaded) - min(s[0] for s in loaded)
print(f"Kernel initialized bytes: {payload:,}")
print(f"Kernel load segments in RAM (including BSS): {reserved:,} bytes")
print(f"Kernel physical address span (including alignment gaps): {span:,} bytes")
print(f"ELF file (includes debugging metadata): {Path(sys.argv[1]).stat().st_size:,} bytes")
if sys.argv[2] != sys.argv[1]:
    print(f"Boot disk image: {Path(sys.argv[2]).stat().st_size:,} bytes")
print("RAM figures exclude firmware, device buffers, and bootloader scratch memory.")
