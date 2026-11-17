"""Static checks do not establish that a kernel actually boots; use make smoke."""
import sys
from pathlib import Path
from elf import read_elf

arch, elf_path, image_path = sys.argv[1:]
machine, entry, segments = read_elf(elf_path)
expected_machine, expected_entry = {
    "x86_64": (62, 0x10000),
    "arm64": (183, 0x40080000),
}[arch]
assert (machine, entry) == (expected_machine, expected_entry)
assert any(addr <= entry < addr + len(data) and flags & 1
           for addr, data, _, flags in segments), "entry must be executable"
assert all(not (flags & 2 and flags & 1) for _, _, _, flags in segments), "RWX segment"
if arch == "x86_64":
    image = Path(image_path).read_bytes()
    assert len(image) == 65 * 512 and image[510:512] == b"\x55\xaa"
    for addr, data, _, _ in segments:
        if data:
            start = 512 + addr - 0x10000
            assert image[start:start + len(data)] == data, "disk/ELF mismatch"
print(f"PASS: {arch} ELF entry, load segments, and image layout")
