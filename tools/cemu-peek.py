#!/usr/bin/env python3
"""Read Wii U guest memory out of a running Cemu via /proc/<pid>/mem.

Cemu maps the whole guest address space into its own process, so this works
at full JIT speed -- no gdb stub, no breakpoints, no --force-interpreter.
The stub has cost three sessions; do not use it.

The base moves every launch (ASLR), so it is derived, never hardcoded: scan
for a string whose guest address is known from the recompiled code and
subtract. A wrong base cannot pass silently -- the anchor matches or it does
not.

    watch  poll addresses at 50 Hz, print only when a value changes
    once    read them a single time
"""
import re
import struct
import sys
import time

ANCHOR = b"ram:/alchemy.xml\x00"
ANCHOR_GUEST = 0x10051308


def cemu_pid():
    import subprocess
    # -x, never -f: a -f pattern has matched this very shell before now
    for name in ("cemu", "Cemu", "cemu_wrapper"):
        r = subprocess.run(["pgrep", "-x", name], capture_output=True, text=True)
        if r.stdout.strip():
            return int(r.stdout.split()[0])
    raise SystemExit("Cemu is not running")


def regions(pid):
    out = []
    with open(f"/proc/{pid}/maps") as f:
        for line in f:
            m = re.match(r"([0-9a-f]+)-([0-9a-f]+) (\S{4})", line)
            if not m or m.group(3)[0] != "r":
                continue
            lo, hi = int(m.group(1), 16), int(m.group(2), 16)
            if hi - lo > 4 << 30:          # skip absurd reservations
                continue
            out.append((lo, hi))
    return out


def find_base(pid):
    with open(f"/proc/{pid}/mem", "rb", 0) as mem:
        for lo, hi in regions(pid):
            try:
                mem.seek(lo)
                buf = mem.read(hi - lo)
            except (OSError, ValueError, OverflowError):
                continue
            i = buf.find(ANCHOR)
            if i >= 0:
                return (lo + i) - ANCHOR_GUEST
    raise SystemExit("anchor not found -- is the title actually loaded?")


def read(mem, base, guest, n):
    mem.seek(base + guest)
    return mem.read(n)


def words(mem, base, guest, count):
    b = read(mem, base, guest, count * 4)
    if len(b) < count * 4:
        return None
    return struct.unpack(f">{count}I", b)      # guest words are BIG-ENDIAN


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "once"
    targets = [(a, int(n)) for a, n in
               (t.split(":") if ":" in t else (t, "8") for t in sys.argv[2:])] or \
              [("0x45f3964", 8), ("0x45f35dc", 8)]
    targets = [(int(a, 16), n) for a, n in targets]

    # Arm before the title finishes loading: the whole bootstrap is about a
    # second under JIT, so wait for Cemu and then for the anchor to appear
    # rather than requiring both to be there already.
    deadline = time.time() + 180
    pid = None
    while time.time() < deadline:
        try:
            pid = cemu_pid()
            break
        except SystemExit:
            time.sleep(0.2)
    if pid is None:
        raise SystemExit("Cemu never appeared")
    print(f"  cemu pid {pid}, waiting for the title to map...", flush=True)

    base = None
    while time.time() < deadline and base is None:
        try:
            base = find_base(pid)
        except SystemExit:
            time.sleep(0.25)
    if base is None:
        raise SystemExit("anchor never appeared -- title did not load")
    print(f"  base 0x{base:x}  (anchor verified)", flush=True)

    with open(f"/proc/{pid}/mem", "rb", 0) as mem:
        last = {}
        t0 = time.time()
        while True:
            for guest, n in targets:
                w = words(mem, base, guest, n)
                if w is None:
                    continue
                s = " ".join(f"{x:08x}" for x in w)
                if last.get(guest) != s:
                    last[guest] = s
                    print(f"  [{time.time()-t0:7.2f}s] 0x{guest:08x}  {s}", flush=True)
            if mode != "watch":
                return
            time.sleep(0.02)


if __name__ == "__main__":
    main()
