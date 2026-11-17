"""Boot the actual guest; send commands only after its prompt appears."""
import os
import selectors
import subprocess
import sys
import time

arch, *command = sys.argv[1:]
output = bytearray()
try:
    guest = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT)
except FileNotFoundError:
    raise SystemExit(f"Missing emulator: {command[0]}. See README.md.")

try:
    with selectors.DefaultSelector() as selector:
        selector.register(guest.stdout, selectors.EVENT_READ)
        deadline = time.monotonic() + 15
        sent = False
        while time.monotonic() < deadline:
            for key, _ in selector.select(timeout=0.1):
                chunk = os.read(key.fileobj.fileno(), 4096)
                if not chunk:
                    raise RuntimeError("guest exited before completing smoke test")
                output.extend(chunk)
            if not sent and b"aios> " in output:
                guest.stdin.write(b"help\narch\nversion\nhalt\n")
                guest.stdin.flush()
                sent = True
            if b"CPU halted." in output:
                break
        else:
            raise RuntimeError("timed out waiting for guest")
    expected = b"x86_64 / BIOS PC" if arch == "x86_64" else b"arm64 / QEMU virt"
    assert expected in output
    assert b"Commands: help, arch, version, clear, halt" in output
    assert b"AI OS 0.1.0 - freestanding kernel" in output
    print(f"PASS: {arch} boot, serial commands, and halt")
except Exception:
    print(output.decode(errors="replace"), file=sys.stderr)
    raise
finally:
    guest.terminate()
    try:
        guest.wait(timeout=2)
    except subprocess.TimeoutExpired:
        guest.kill()
        guest.wait()
