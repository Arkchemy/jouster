#!/usr/bin/env python3
"""Find every igHeapMemoryPool in a running retail SSA and walk its TLSF chain.

Why: jouster's Default pool loses the tail of its 5 MB arena -- the physical
block chain stops 3.79 MB short of the sentinel. The recompiled tlsf_memalign
is correct in isolation (see conquertron/hosttest), so either it diverges only
after specific heap state, or it is being fed a different call sequence than
retail. Retail's own heaps distinguish those: if the real game's pools all
reach their sentinels with a similar block profile, the allocator is fine and
the inputs differ.

Objects are found by scanning for the class vtable, never by translating a
jouster heap address -- those do not correspond and reading one returns a
convincing false zero. See jouster-vs-retail-addresses.

Layout, all verified against the retail binary rather than assumed:
  igMemoryPool  +0x10 _address  +0x14 _size  +0x18 _alignment
                +0x2c blocksAllocated  +0x34 userAllocated   (updateStatistics)
  TLSF control  0xc70 bytes; first header at control+0xc70
                header +4 = size|flags, bit 0 free, next = block + 4 + size
"""
import struct, sys, importlib.util, os

# cemu-peek.py has a hyphen in its name, so load it by path.
_spec = importlib.util.spec_from_file_location(
    "cemu_peek", os.path.join(os.path.dirname(__file__), "cemu-peek.py"))
cemu_peek = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cemu_peek)

VTBL_HEAP = 0x100541DC          # __vtbl__Q2_4Core16igHeapMemoryPool, from .symtab
CTRL_SIZE = 0xC70


def walk(mem, base, ctrl, size):
    """Physical block walk; returns (blocks, used, free, span, ok)."""
    b, end, n, used, freeb = ctrl + CTRL_SIZE, ctrl + size, 0, 0, 0
    while True:
        if n >= 200000 or b < ctrl or b + 8 > end:
            return n, used, freeb, b, False        # ran out of the arena
        w = cemu_peek.words(mem, base, b + 4, 1)
        if w is None:
            return n, used, freeb, b, False
        sz = w[0] & ~3
        if sz == 0:
            return n, used, freeb, b, True
        if w[0] & 1: freeb += sz
        else:        used  += sz
        n += 1
        b += 4 + sz


def main():
    pid = cemu_peek.cemu_pid()
    base = cemu_peek.find_base(pid)
    print(f"cemu pid {pid}, guest base 0x{base:x}\n")
    needle = struct.pack(">I", VTBL_HEAP)
    found = []
    with open(f"/proc/{pid}/mem", "rb", 0) as mem:
        # MEM2 is where game heaps live; scan the mapped guest range broadly.
        for lo, hi in cemu_peek.regions(pid):
            try:
                mem.seek(lo); buf = mem.read(hi - lo)
            except (OSError, ValueError, OverflowError):
                continue
            i = buf.find(needle)
            while i >= 0:
                guest = (lo + i) - base
                if 0 < guest < 0x40000000 and i % 4 == 0:
                    found.append(guest)
                i = buf.find(needle, i + 1)

        print(f"{len(found)} igHeapMemoryPool instance(s)\n")
        for obj in sorted(set(found)):
            w = cemu_peek.words(mem, base, obj, 16)
            if not w:
                continue
            addr, size, align = w[4], w[5], w[6]
            blocks_acct, user = w[11], w[13]
            if not addr or not size or size > 0x20000000:
                continue
            n, used, freeb, span, ok = walk(mem, base, addr, size)
            sentinel = addr + size - 8
            print(f"  pool 0x{obj:08x}  arena 0x{addr:08x} size 0x{size:x} align {align}")
            print(f"    accounting: blocks={blocks_acct} userAllocated={user:,}")
            print(f"    walk: blocks={n} used={used:,} free={freeb:,} span=0x{span:08x}")
            if not ok:
                print(f"    !! walk left the arena")
            elif span == sentinel:
                print(f"    OK  reaches the sentinel, chain spans the whole arena")
            else:
                print(f"    !! ends 0x{span:08x}, sentinel 0x{sentinel:08x}"
                      f" -- {sentinel - span:,} bytes orphaned")
            print()


if __name__ == "__main__":
    main()
