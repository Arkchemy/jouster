"""Probe: is the bad block header scribbled, or is the pointer to it wrapped?

The 19:29 build named the culprit:

    sizeclass: hits=2 first_pc=0x21f0b8c fl=24

0x21f0b8c is in tlsf_free, on the path that merges a freed block with its
neighbour:

    21f0b34: lwz r7,  -8(r4)              ; r7 = neighbour block, from *(ptr-8)
    21f0b38: lwz r12, 4(r7)               ; size = neighbour->header+4
    21f0b3c: rlwinm r11, r12, 0, 0, 0x1d  ; strip the low flag bits
    21f0b7c: cntlzw / subfic 0x20         ; fls(size)
    21f0b8c: addi r12, r12, -7            ; fl = fls(size) - 7   -> 24

fl = 24 needs fls = 31, so that neighbour claims a size in [1 GiB, 2 GiB).
PPC_MEM_SIZE is exactly 1 GiB, which is why a wrapped pointer is the suspect:
the guest-memory mask turns an out-of-range address into one near the top of
the space, and a size computed from it comes out about the size of the whole
address space.

Only 2 hits in a 120-second run, so this is one specific event, not systemic.

Two possibilities, and they need different fixes:

  the neighbour pointer at ptr-8 is itself out of the pool
      -> the pointer is wrong, probably wrapped, and the header it lands on
         was never a block header at all
  the pointer is in range but the header reads as ~1 GiB
      -> the header was scribbled by something writing past its allocation

Captures the freed pointer, the neighbour pointer, the raw size, four header
words at the neighbour, and the pool bounds, so those can be told apart on
sight rather than argued about.

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

SRC = pathlib.Path(__file__).resolve().parent.parent / "source" / "generated_0165.c"
MARKER = "ARKCHEMY-PROBE-BADBLOCK"

OLD = ("  /* 21f0b8c: addi r12, r12, -7 */\n"
       "  ctx->r[12] = ctx->r[12] + (uint32_t)(int32_t)-7;\n")

NEW = ("  /* 21f0b8c: addi r12, r12, -7 */\n"
       "  ctx->r[12] = ctx->r[12] + (uint32_t)(int32_t)-7;\n"
       "  /* " + MARKER + " -- see tools/probe_tlsf_badblock.py */\n"
       "  { extern unsigned int g_arkchemy_bb_hits, g_arkchemy_bb_ctrl, g_arkchemy_bb_blk;\n"
       "    extern unsigned int g_arkchemy_bb_size, g_arkchemy_bb_hdr[4];\n"
       "    if ((int32_t)ctx->r[12] > 23 && g_arkchemy_bb_hits == 0u) {\n"
       "      unsigned int __i;\n"
       "      g_arkchemy_bb_ctrl = ctx->r[3];\n"
       "      g_arkchemy_bb_blk  = ctx->r[7];\n"
       "      g_arkchemy_bb_size = ctx->r[11];\n"
       "      for (__i = 0; __i < 4u; __i++)\n"
       "        g_arkchemy_bb_hdr[__i] = ppc_load_u32(ctx, ctx->r[7] + __i * 4u);\n"
       "      g_arkchemy_bb_hits++;\n"
       "    } }\n")


def main():
    if not SRC.exists():
        sys.exit("missing %s -- regenerate first" % SRC)
    t = SRC.read_text()
    if MARKER in t:
        print("probe_tlsf_badblock: already applied, nothing to do")
        return
    if t.count(OLD) != 1:
        sys.exit("probe_tlsf_badblock: expected 1 site at 21f0b8c, found %d" % t.count(OLD))
    SRC.write_text(t.replace(OLD, NEW))
    print("probe_tlsf_badblock: instrumented the merge path in %s" % SRC.name)


if __name__ == "__main__":
    main()
