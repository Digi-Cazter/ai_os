"""Lay out a BIOS disk: sector 0 bootloader, sectors 1..64 kernel, padding."""
import sys
from pathlib import Path
from elf import read_elf

boot = Path(sys.argv[1]).read_bytes()
machine, entry, segments = read_elf(sys.argv[2])
assert len(boot) == 512 and boot[-2:] == b"\x55\xaa", "bad BIOS boot sector"
assert machine == 62 and entry == 0x10000, "wrong kernel target or entry"
payload = bytearray(64 * 512)
for address, data, _, _ in segments:
    if not data:
        continue  # BSS has a RAM footprint, but no bytes stored on disk.
    offset = address - 0x10000
    if offset < 0 or offset + len(data) > len(payload):
        raise SystemExit("Kernel file exceeds the stage-1 loader's 32 KiB limit")
    payload[offset:offset + len(data)] = data
image = boot + payload
Path(sys.argv[3]).write_bytes(image)
