"""Probe: is the TLSF free list circular, and how long is it?

After the typeIndex-2 fix the boot advanced from class 54 to 61 and execution
went from 34,487 calls to 150,839. The new stall is a leaf loop in
tlsf_largest_free_block_size (0x21f0450), reached from
Core::igMemoryPool::reallocCommon:

    21f04b4: lwz r7, 0x74(r6)   ; free list head
    21f04b8: lwz r8, 4(r7)      ; ->size
  L_21f04c8:
    21f04c8: cmplw r0, r8       ; track the largest
    21f04cc: lwz r7, 8(r7)      ; ->next
    21f04d4: mr r0, r8
    21f04d8: cmplw r7, r3       ; back at the control block?
    21f04dc: beq done
    21f04e0: lwz r8, 4(r7)
    21f04e4: b L_21f04c8

The list is circular by design and the walk ends only when ->next arrives back
at the control block in r3. If the chain is corrupt, or its terminator points
at something other than r3, the walk never ends. Registers at the stall:
r3=0x45002e0 (control), r6=0x4500b5c, r4=0x18, r5=0xf.

What this cannot currently distinguish is a genuine cycle from a merely very
long list, so it counts. On exceeding the cap it records the control pointer,
the first few nodes visited and the iteration count, then bails out returning
the largest block found so far -- which is the honest answer for the part of
the list actually walked, and lets the boot continue so the NEXT blocker
becomes visible in the same run rather than costing another cycle.

Reading the result:
  iters at the cap, nodes repeating   -> a real cycle; the free list is
                                         corrupt and the question becomes who
                                         corrupted it
  iters at the cap, nodes all distinct-> a huge but valid list, and the cap is
                                         simply too low
  probe never fires                   -> the loop terminated normally and the
                                         stall is elsewhere

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

SRC = pathlib.Path(__file__).resolve().parent.parent / "source" / "generated_0165.c"
MARKER = "ARKCHEMY-PROBE-TLSF"
CAP = 1000000

OLD = "  goto L_21f04c8;\n  L_21f04e8: ;\n"
NEW = (
    "  /* " + MARKER + " -- see tools/probe_tlsf_freelist.py */\n"
    "  {\n"
    "    extern unsigned int g_arkchemy_tlsf_iters, g_arkchemy_tlsf_tripped;\n"
    "    extern unsigned int g_arkchemy_tlsf_ctrl, g_arkchemy_tlsf_nodes[6];\n"
    "    unsigned int __n = ++g_arkchemy_tlsf_iters;\n"
    "    if (__n <= 6u) g_arkchemy_tlsf_nodes[__n - 1u] = ctx->r[7];\n"
    "    if (__n > " + str(CAP) + "u) {\n"
    "      g_arkchemy_tlsf_tripped++;\n"
    "      g_arkchemy_tlsf_ctrl = ctx->r[3];\n"
    "      g_arkchemy_tlsf_iters = 0u;\n"
    "      goto L_21f04e8;   /* bail out with the largest seen so far */\n"
    "    }\n"
    "  }\n"
    "  goto L_21f04c8;\n"
    "  L_21f04e8: ;\n"
)


def main():
    if not SRC.exists():
        sys.exit("missing %s -- regenerate first" % SRC)
    t = SRC.read_text()
    if MARKER in t:
        print("probe_tlsf_freelist: already applied, nothing to do")
        return
    if t.count(OLD) != 1:
        sys.exit("probe_tlsf_freelist: expected 1 back-edge, found %d" % t.count(OLD))
    SRC.write_text(t.replace(OLD, NEW))
    print("probe_tlsf_freelist: instrumented the walk in %s" % SRC.name)


if __name__ == "__main__":
    main()
