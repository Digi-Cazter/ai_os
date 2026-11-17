"""AI OS telemetry v1 decoder. Standard library only; no QEMU dependency.

The same decoder accepts fragmented bytes from a file, pipe, or physical serial
port. No packet contains a native C struct. See docs/05-live-telemetry.md.
"""
import binascii
import struct

MAGIC = b"\xa5\x5a"
HEADER = struct.Struct("<2sBBHIQ")
FIELDS = [
    ("sensor_sequence", "I"), ("distance_mm", "I"), ("sensor_valid", "I"),
    ("sampled_at_ms", "Q"), ("controller_sequence", "I"),
    ("decision_at_ms", "Q"), ("decision_sample_age_ms", "Q"),
    ("target_speed_mm_s", "I"), ("reason", "I"),
    ("queue_count", "I"), ("queue_high_water", "I"),
    ("published", "Q"), ("consumed", "Q"), ("queue_dropped", "Q"),
    ("sensor_missed", "Q"), ("controller_missed", "Q"), ("telemetry_dropped", "Q"),
]
PAYLOAD = struct.Struct("<" + "".join(fmt for _, fmt in FIELDS))
FRAME_SIZE = HEADER.size + PAYLOAD.size + 2
assert PAYLOAD.size == 104 and FRAME_SIZE == 124
REASONS = {0: "WAITING", 1: "CLEAR", 2: "OBSTACLE", 3: "STALE", 4: "INVALID"}


class Decoder:
    """Recover framing after noise, partial writes, or damaged packets.

    CLI reads at most 4096 bytes per feed. After processing, the retained buffer
    is at most one incomplete frame. Unknown versions/kinds are rejected.
    """
    def __init__(self):
        self.buffer = bytearray()
        self.bad_crc = 0
        self.bad_header = 0
        self.discarded_bytes = 0

    def feed(self, data):
        self.buffer.extend(data)
        records = []
        while True:
            start = self.buffer.find(MAGIC)
            if start < 0:
                keep = 1 if self.buffer.endswith(MAGIC[:1]) else 0
                discarded = len(self.buffer) - keep
                self.discarded_bytes += discarded
                del self.buffer[:discarded]
                break
            if start:
                self.discarded_bytes += start
                del self.buffer[:start]
            if len(self.buffer) < HEADER.size:
                break
            _, version, kind, length, sequence, timestamp = HEADER.unpack_from(self.buffer)
            if version != 1 or kind != 1 or length != PAYLOAD.size:
                self.bad_header += 1
                self.discarded_bytes += 1
                del self.buffer[0]
                continue
            if len(self.buffer) < FRAME_SIZE:
                break
            expected = struct.unpack_from("<H", self.buffer, FRAME_SIZE - 2)[0]
            actual = binascii.crc_hqx(self.buffer[2:FRAME_SIZE - 2], 0xffff)
            if expected != actual:
                self.bad_crc += 1
                self.discarded_bytes += 1
                del self.buffer[0]
                continue
            values = PAYLOAD.unpack_from(self.buffer, HEADER.size)
            record = dict(zip((name for name, _ in FIELDS), values))
            record.update(record_sequence=sequence, target_ms=timestamp)
            records.append(record)
            del self.buffer[:FRAME_SIZE]
        return records


class SequenceTracker:
    """Count observable gaps; recognize clock reversal/sequence reset.

    A newly attached observer cannot know about losses before its first packet.
    A reset is only inferred, not a guaranteed physical boot identifier.
    """
    def __init__(self):
        self.last_sequence = None
        self.last_time = None
        self.missing = 0
        self.duplicates = 0

    def accept(self, record):
        sequence, timestamp = record["record_sequence"], record["target_ms"]
        reset = False
        if self.last_sequence is not None:
            delta = (sequence - self.last_sequence) & 0xffffffff
            reset = timestamp < self.last_time or delta > 0x80000000
            if reset:
                self.missing = self.duplicates = 0
            elif delta == 0:
                self.duplicates += 1
            else:
                self.missing += delta - 1
        self.last_sequence, self.last_time = sequence, timestamp
        return reset
