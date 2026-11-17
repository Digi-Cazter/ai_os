"""Live terminal observation, JSONL recording, and replay of telemetry v1.

Examples:
  python3 scripts/telemetry_view.py --input build/x86_64/gcc/telemetry.bin --follow
  python3 scripts/telemetry_view.py --serial /dev/ttyUSB0 --record build/session.jsonl
  python3 scripts/telemetry_view.py --replay build/session.jsonl
"""
import argparse
from collections import deque
import contextlib
import datetime
import json
import os
from pathlib import Path
import shutil
import sys
import time
import uuid

from telemetry import Decoder, REASONS, SequenceTracker

RESET = object()


def file_chunks(path, follow):
    with open(path, "rb") if path != "-" else contextlib.nullcontext(sys.stdin.buffer) as stream:
        while True:
            if path != "-" and os.fstat(stream.fileno()).st_size < stream.tell():
                stream.seek(0)
                yield RESET
            data = os.read(stream.fileno(), 4096)
            if data:
                yield data
            elif not follow or path == "-":
                return
            else:
                yield None
                time.sleep(0.05)


def serial_chunks(path):
    # Linux/POSIX adapter. Do not apply terminal settings to regular recordings.
    import termios
    import tty
    fd = os.open(path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    original = None
    try:
        original = termios.tcgetattr(fd)
        tty.setraw(fd)
        attributes = termios.tcgetattr(fd)
        attributes[4] = attributes[5] = termios.B115200
        attributes[2] |= termios.CLOCAL | termios.CREAD
        termios.tcsetattr(fd, termios.TCSANOW, attributes)
        while True:
            try:
                data = os.read(fd, 4096)
            except BlockingIOError:
                data = b""
            yield data or None
            if not data:
                time.sleep(0.02)
    finally:
        if original is not None:
            termios.tcsetattr(fd, termios.TCSANOW, original)
        os.close(fd)


class View:
    def __init__(self, plain, label, replay=False):
        self.interactive = sys.stdout.isatty() and not plain
        self.label = label
        self.mode = "REPLAY" if replay else "LIVE"
        self.history = deque(maxlen=48)
        self.last_sensor = None
        self.last_record = None
        self.last_received = time.monotonic()
        self.last_warning = 0
        if self.interactive:
            print("\033[2J\033[?25l", end="", flush=True)

    def reset(self):
        self.history.clear()
        self.last_sensor = None
        self.last_record = None
        self.last_received = time.monotonic()

    def show(self, record, tracker, decoder):
        self.last_record = record
        self.last_received = time.monotonic()
        if not self.interactive:
            print(f"t={record['target_ms']:>6}ms seq={record['sensor_sequence']:>4} "
                  f"distance={record['distance_mm']:>4}mm queue={record['queue_count']}/4 "
                  f"speed={record['target_speed_mm_s']}mm/s "
                  f"reason={REASONS.get(record['reason'], 'UNKNOWN')} "
                  f"queue_drops={record['queue_dropped']} tx_drops={record['telemetry_dropped']}",
                  flush=True)
            return
        width = max(8, min(40, shutil.get_terminal_size().columns - 26))
        if self.last_sensor != record["sensor_sequence"] and record["sensor_valid"]:
            self.history.append(record["distance_mm"])
            self.last_sensor = record["sensor_sequence"]
        palette = " .:-=+*#%@"
        history = "".join(palette[min(9, max(0, value * 9 // 2000))]
                          for value in list(self.history)[-width:])
        filled = min(width, max(0, record["distance_mm"] * width // 2000))
        bar = "#" * filled + "." * (width - filled)
        queue = "#" * min(4, record["queue_count"]) + "." * max(0, 4 - record["queue_count"])
        reason = REASONS.get(record["reason"], "UNKNOWN")
        lines = [
            f"AI OS | {self.mode} telemetry v1 | Ctrl-C exits viewer",
            f"Source: {self.label}",
            f"Target time: {record['target_ms']} ms (tick-derived)",
            f"Sensor #{record['sensor_sequence']}: {record['distance_mm']} mm  valid={record['sensor_valid']}",
            f"Distance 0..2000 [{bar}]",
            f"Received history [{history}]",
            f"Queue [{queue}] {record['queue_count']}/4  high-water={record['queue_high_water']}",
            f"Published={record['published']} consumed={record['consumed']} dropped={record['queue_dropped']}",
            f"Controller: {reason}  target={record['target_speed_mm_s']} mm/s",
            f"Decision at {record['decision_at_ms']} ms, sample #{record['controller_sequence']}",
            f"Sample age at that decision: {record['decision_sample_age_ms']} ms",
            f"Skipped releases: sensor={record['sensor_missed']} controller={record['controller_missed']}",
            f"Telemetry drops={record['telemetry_dropped']} observed gaps={record.get('rx_missing', tracker.missing)}",
            f"Bad CRC={record.get('rx_bad_crc', decoder.bad_crc)} headers={record.get('rx_bad_header', decoder.bad_header)} discarded bytes={record.get('rx_discarded_bytes', decoder.discarded_bytes)}",
            "Synthetic distance / stored speed decision; no physical motion.",
        ]
        print("\033[H" + "\n".join(lines) + "\033[J", end="", flush=True)

    def waiting(self):
        age = time.monotonic() - self.last_received
        if age >= 1 and time.monotonic() - self.last_warning >= 1:
            self.last_warning = time.monotonic()
            message = f"No new telemetry for {age:.0f}s of host time; target status unknown."
            if self.interactive:
                print("\033[H\033[2K" + message, end="", flush=True)
            else:
                print(message, file=sys.stderr)

    def close(self):
        if self.interactive:
            print("\n\033[?25h", end="", flush=True)


def replay(path, speed, view):
    tracker, decoder = SequenceTracker(), Decoder()
    previous_time = previous_session = None
    with open(path, encoding="utf-8") as stream:
        while True:
            line = stream.readline(65537)
            if not line:
                break
            if len(line) > 65536:
                raise ValueError("recording line exceeds 64 KiB")
            event = json.loads(line)
            if event.get("kind") != "snapshot":
                continue
            if event.get("schema") != "aios.telemetry.v1":
                raise ValueError("unsupported recording schema")
            session = event["session_id"]
            timestamp = event["host_monotonic_ns"]
            if session != previous_session:
                previous_time = None
                tracker = SequenceTracker()
                view.reset()
            if previous_time is not None:
                time.sleep(max(0, timestamp - previous_time) / 1e9 / speed)
            if tracker.accept(event):
                view.reset()
            view.show(event, tracker, decoder)
            previous_time, previous_session = timestamp, session


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument("--input", help="raw telemetry file, or - for a pipe")
    sources.add_argument("--serial", help="POSIX serial port, configured to 115200 8N1")
    sources.add_argument("--replay", help="JSONL recording made by --record")
    parser.add_argument("--follow", action="store_true", help="follow a growing raw file")
    parser.add_argument("--record", help="write decoded snapshots to JSONL (new file only)")
    parser.add_argument("--speed", type=float, default=1, help="replay speed multiplier")
    parser.add_argument("--plain", action="store_true", help="print one line per snapshot")
    parser.add_argument("--label", default="Unspecified source", help="user-supplied source label")
    args = parser.parse_args()
    if not 0 < args.speed < float("inf"):
        parser.error("--speed must be finite and positive")
    if args.replay and args.record:
        parser.error("--record is for live input; copy recordings explicitly")
    view = View(args.plain, args.label, replay=bool(args.replay))
    try:
        if args.replay:
            replay(args.replay, args.speed, view)
            return
        decoder, tracker = Decoder(), SequenceTracker()
        chunks = serial_chunks(args.serial) if args.serial else file_chunks(args.input, args.follow)
        destination = open(args.record, "x", encoding="utf-8") if args.record else contextlib.nullcontext()
        with destination as recording, contextlib.closing(chunks):
            session = str(uuid.uuid4())

            def begin_session():
                if recording:
                    recording.write(json.dumps({"schema": "aios.telemetry.v1", "kind": "session",
                        "session_id": session, "source": args.label,
                        "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                        "clock": "target tick-derived ms; host receive clocks recorded separately"}) + "\n")
                    recording.flush()

            begin_session()
            for chunk in chunks:
                if chunk is RESET:
                    decoder, tracker = Decoder(), SequenceTracker()
                    session = str(uuid.uuid4())
                    view.reset()
                    begin_session()
                    continue
                if chunk is None:
                    view.waiting()
                    continue
                for record in decoder.feed(chunk):
                    if tracker.accept(record):
                        session = str(uuid.uuid4())
                        view.reset()
                        begin_session()
                    record.update(schema="aios.telemetry.v1", kind="snapshot", session_id=session,
                                  host_received_ns=time.time_ns(), host_monotonic_ns=time.monotonic_ns(),
                                  rx_missing=tracker.missing, rx_bad_crc=decoder.bad_crc,
                                  rx_bad_header=decoder.bad_header, rx_discarded_bytes=decoder.discarded_bytes)
                    if recording:
                        recording.write(json.dumps(record, separators=(",", ":")) + "\n")
                        recording.flush()
                    view.show(record, tracker, decoder)
            if decoder.buffer:
                print(f"Trailing incomplete packet: {len(decoder.buffer)} bytes", file=sys.stderr)
            print(f"Decoder: CRC errors={decoder.bad_crc}, header errors={decoder.bad_header}, "
                  f"discarded bytes={decoder.discarded_bytes}, gaps={tracker.missing}", file=sys.stderr)
    except KeyboardInterrupt:
        pass
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"telemetry: {error}\n")
    finally:
        view.close()


if __name__ == "__main__":
    main()
