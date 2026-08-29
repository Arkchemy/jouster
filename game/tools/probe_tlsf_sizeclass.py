"""Probe: which TLSF operation creates an out-of-range size class?

Measured on hardware (19:13 build):

    tlsfc: calls=2 bail=0 mem=0x45002e0 size=0x500000
    tlsfh: ctrl=0x45002e0 fl=0x1010021 sl=0x0 slot=0x4500fac idx=790
           neigh=[0x0,0x200,0x4501020,0x18]

tlsf_create ran on this very control block and did not bail, so the pool was
initialised. The fault is the first-level bitmap: 0x1010021 has bits 0, 5, 16
and 24 set, and the sl-bitmap array spans +0x14..+0x74, i.e. (0x74-0x14)/4 =
24 words, so valid fl is 0..23. Bit 24 is one past the end. The search takes
fl=24, computes idx = 24*32 + 22 = 790, and reads a "head" at ctrl+0x74+3160 =
0x4500fac -- past the table, in block metadata (the neighbours read 0x200 and
0x18, which are sizes, not list heads). That slot is 0, the walk never sees
the sentinel, and it spins.

The size-class mapping, read off the code rather than assumed:

    cntlzw r10, r0          ; clz(size)
    subfic r10, r10, 0x20   ; fls(size)
    addi   r8,  r10, -7     ; fl = fls(size) - 7
    slw    r10, r0,  r8     ; 1 << fl
    or     r4,  r4,  r10    ; fl_bitmap |= bit

So fl = fls(size) - 7, and fl = 24 requires fls = 31, i.e. a block size in
[1 GiB, 2 GiB).

This kills the obvious theory. The pool is 5 MiB here against 1 MiB in the
real game, which looked like the answer -- but 5 MiB gives fls = 23 and fl =
16, and bit 16 is legitimately set. Pool size is not the problem.

PPC_MEM_SIZE is 0x40000000, exactly 1 GiB, and fls(0x40000000) = 31 -> fl =
24. A block whose size field is about the size of the entire guest address
space is the signature of a pointer difference where one side was wrapped by
the guest-memory mask -- the same masking that has turned three crashes into
silent spins today.

What is not known is which operation inserts that block. There are 18 mapping
sites across tlsf_create, tlsf_free, tlsf_memalign, tlsf_realloc and
tlsf_check_heap. This patches all of them mechanically and records the first
one to compute fl > 23, with its address, so the responsible function is named
rather than guessed at.

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, re, sys

SRC = pathlib.Path(__file__).resolve().parent.parent / "source" / "generated_0165.c"
MARKER = "ARKCHEMY-PROBE-SIZECLASS"

SITE = re.compile(
    r'(  /\* ([0-9a-f]+): addi r(\d+), r(\d+), -7 \*/\n'
    r'  ctx->r\[\3\] = ctx->r\[\4\] \+ \(uint32_t\)\(int32_t\)-7;\n)')


def main():
    if not SRC.exists():
        sys.exit("missing %s -- regenerate first" % SRC)
    text = SRC.read_text()
    if MARKER in text:
        print("probe_tlsf_sizeclass: already applied, nothing to do")
        return

    # only inside tlsf_* functions
    out, count = [], 0
    fn = None
    for line in text.split('\n'):
        m = re.match(r'void ppc_(\w+)', line)
        if m:
            fn = m.group(1)
        out.append(line)
    text_lines = text.split('\n')
    result = []
    fn = None
    i = 0
    while i < len(text_lines):
        line = text_lines[i]
        m = re.match(r'void ppc_(\w+)', line)
        if m:
            fn = m.group(1)
        result.append(line)
        mm = re.match(r'  /\* ([0-9a-f]+): addi r(\d+), r(\d+), -7 \*/$', line)
        if mm and fn and fn.startswith('tlsf') and i + 1 < len(text_lines):
            nxt = text_lines[i + 1]
            expect = "  ctx->r[%s] = ctx->r[%s] + (uint32_t)(int32_t)-7;" % (mm.group(2), mm.group(3))
            if nxt == expect:
                result.append(nxt)
                result.append("  /* %s */" % MARKER)
                result.append("  { extern unsigned int g_arkchemy_sc_hits, g_arkchemy_sc_pc, g_arkchemy_sc_fl;")
                result.append("    if ((int32_t)ctx->r[%s] > 23) {" % mm.group(2))
                result.append("      if (g_arkchemy_sc_hits == 0u) {")
                result.append("        g_arkchemy_sc_pc = 0x%su; g_arkchemy_sc_fl = ctx->r[%s];" % (mm.group(1), mm.group(2)))
                result.append("      }")
                result.append("      g_arkchemy_sc_hits++;")
                result.append("    } }")
                i += 2
                count += 1
                continue
        i += 1
    if count == 0:
        sys.exit("probe_tlsf_sizeclass: found no mapping sites to patch")
    SRC.write_text('\n'.join(result))
    print("probe_tlsf_sizeclass: instrumented %d size-class mapping sites" % count)


if __name__ == "__main__":
    main()
