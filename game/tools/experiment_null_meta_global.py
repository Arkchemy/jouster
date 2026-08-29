#!/usr/bin/env python3
"""EXPERIMENT (not a claimed fix): survive the NULL metaobject global.

igDataList::setCapacity looks a handler up by type index:

    215db7c: bctrl                   <- virtual call, result in r3
    215db80: lis  r12, 0x1013        <- &.bss+321000, synthetic 435928
    215db84: lwz  r12, 0x45e8(r12)   <- r12 = *global   (a metaobject)
    215db88: lha  r6,  0xc(r12)      <- r6  = metaobject->typeIndex
    215db8c: lwz  r0,  0x14(r3)      <- the table
    215db94: lwzx r27, r10, r0       <- table[typeIndex]

That global is read exactly once in the whole 8.7M-line tree and written
nowhere -- verified by seven independent scans covering folded bases in
synthetic space, EA simulation in real PowerPC space, literal stores, data
imports, r13/r2 small-data, indexed stwx/sthx/stbx, and address-taken-then-
stored-interprocedurally, plus 100.00% text coverage proving no function is
missing and zero .bss offsets mapping to two synthetic addresses proving reads
and writes were not folded apart. See
test-results/2026-08-29-global-writer-search-exhaustive.txt.

So r12 is 0, and `lha r6, 0xc(r12)` reads guest address 12 -- low memory that
other code scribbles on. Hardware confirms it: the SETCAP_REG probe reports
index=0x0 (entry=0x440504c, valid), index=0x21c (entry=0) and index=0x4a0
(entry=0). Registration proceeds while the garbage happens to be 0 and breaks
when it is 540 or 1184, which is why the boot dies at class 54 of 1007 and
then hangs forever in igStringPool::remove walking a bucket chain with no NULL
check, at a bucket index of 0x811C9DC5 -- the raw FNV-1a basis, because
`hash % bucketCount` with bucketCount 0 evaluates to hash.

WHAT THIS DOES, AND WHAT IT DOES NOT
------------------------------------
When the global is NULL this substitutes typeIndex 2 instead of whatever
happens to be lying at guest address 12.

2 is not a guess. Cemu, broken at 0x215db88 on a healthy boot, shows the
global holding 0x115DE3B8 -- a heap-allocated metaobject, so the value is
written at runtime, which is why nothing static writes it -- and that
object's typeIndex halfword at +0xc reads 0x0002. An earlier version of this
script used 0, which was only ever "a value hardware happened to show
resolving to a non-NULL entry".

This is a probe, not a repair. The correct value is whatever metaobject that
global is supposed to hold, and that is still unknown -- nothing in the
program writes it, which most likely means conquertron folded this reference
to the wrong symbol. Treat a boot that gets further as evidence about the
mechanism, not as the bug being fixed. If registration walks past 54 toward
1007, the chain is confirmed end to end and the remaining work is finding the
right metaobject. If it stalls somewhere new, that is the next real blocker.

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

SRC = pathlib.Path(__file__).resolve().parent.parent / "source" / "generated_0156.c"
MARKER = "ARKCHEMY-EXPERIMENT-NULLMETA"

OLD = (
    "  /* 215db88: lha r6, 0xc(r12) */\n"
    "  ctx->r[6] = (uint32_t)(int32_t)(int16_t)ppc_load_u16(ctx, ctx->r[12] + (int32_t)12);\n"
)

NEW = (
    "  /* 215db88: lha r6, 0xc(r12) */\n"
    "  /* " + MARKER + " -- see tools/experiment_null_meta_global.py */\n"
    "  if (ctx->r[12] == 0u) {\n"
    "    extern unsigned int g_arkchemy_nullmeta_hits;\n"
    "    g_arkchemy_nullmeta_hits++;\n"
    "    ctx->r[6] = 2u;\n"
    "  } else {\n"
    "    ctx->r[6] = (uint32_t)(int32_t)(int16_t)ppc_load_u16(ctx, ctx->r[12] + (int32_t)12);\n"
    "  }\n"
)


def main():
    if not SRC.exists():
        sys.exit("missing %s -- regenerate first" % SRC)
    text = SRC.read_text()
    if MARKER in text:
        print("experiment_null_meta_global: already applied, nothing to do")
        return
    if text.count(OLD) != 1:
        sys.exit(
            "experiment_null_meta_global: expected exactly 1 match for the "
            "215db88 read site, found %d -- the generated code changed shape, "
            "so refusing to guess" % text.count(OLD)
        )
    SRC.write_text(text.replace(OLD, NEW))
    print("experiment_null_meta_global: patched the 215db88 read site in %s" % SRC.name)


if __name__ == "__main__":
    main()
