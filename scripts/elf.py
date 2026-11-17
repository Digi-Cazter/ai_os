"""Minimal ELF64 reader used to inspect our own little-endian kernel files."""
import struct
from pathlib import Path


def read_elf(path):
    data = Path(path).read_bytes()
    if data[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("expected little-endian ELF64")
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    segments = []
    for i in range(header[10]):
        fields = struct.unpack_from("<IIQQQQQQ", data, header[5] + i * header[9])
        kind, flags, offset, virtual, physical, size, memory_size, alignment = fields
        if kind == 1:  # PT_LOAD: bytes the loader places in RAM
            if size > memory_size or offset + size > len(data):
                raise ValueError("invalid load segment")
            segments.append((physical, data[offset:offset + size], memory_size, flags))
    return header[2], header[4], segments
