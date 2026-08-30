"""Probe: who calls createInstanceInPlace with a static-data metaobject?

The corruption is understood. igMetaObject::createInstanceInPlace bumps its own
`this` refcount at +0x18:

    2160cd0: mr     r29, r3          ; this, the metaobject
    2160dac: addi   r11, r29, 0x18
    2160dbc: stwcx. r9, 0, r11       ; atomic increment

and the arithmetic is exact: the corrupted global is .data+5336, the offset is
0x18, so the metaobject it was handed is .data+5312 -- a static-data address
rather than a heap object. Bumping its refcount writes onto the memory-context
pointer 24 bytes later, and everything else follows from that single value.

Nothing in that sequence is mistranslated. The input is wrong, so the question
is entirely about the caller.

Catches the first call whose metaobject is NOT a heap pointer. Our MEM2 heap
starts at 0x04000000 and real metaobjects observed in this build sit around
0x442xxxx, so anything below that is static data or worse.

Records ctx->lr, which is the true call site -- every emitted bctrl and bl
writes its own return address there on the line above the call. Deliberately
NOT g_ppc_current_pc: that is set at function entry, never restored on return,
and over this investigation it has named _main, _savegpr_14_l,
igMetaField::construct, a field-array loop, igPool::grow and finally a
nine-instruction getter, none of which were responsible.

    badmeta: hits=%u lr=0x%x meta=0x%x mem=0x%x pool=0x%x w=[4 words at meta]

  lr             the caller, which is the whole question
  meta           expected around .data+5312 (13504)
  w              what actually lives there, which says whether it is a
                 mistaken pointer to a real structure or arithmetic gone wrong

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

MARKER = "ARKCHEMY-PROBE-BADMETA"
SIG = "void ppc_createInstanceInPlace__Q2_4Core12igMetaObjectCFPvPQ2_4Core12igMemoryPool(PpcContext *ctx) {\n"
HEAP_BASE = 0x04000000

BODY = ("  /* " + MARKER + " -- see tools/probe_bogus_metaobject.py */\n"
        "  if (ctx->r[3] < %du) {\n" % HEAP_BASE
        + "    extern unsigned int g_arkchemy_bm_hits, g_arkchemy_bm_lr, g_arkchemy_bm_meta;\n"
        "    extern unsigned int g_arkchemy_bm_mem, g_arkchemy_bm_pool, g_arkchemy_bm_w[4];\n"
        "    if (g_arkchemy_bm_hits == 0u) {\n"
        "      unsigned int __i;\n"
        "      g_arkchemy_bm_lr = ctx->lr;\n"
        "      g_arkchemy_bm_meta = ctx->r[3];\n"
        "      g_arkchemy_bm_mem = ctx->r[4];\n"
        "      g_arkchemy_bm_pool = ctx->r[5];\n"
        "      if (ctx->r[3])\n"
        "        for (__i = 0; __i < 4u; __i++)\n"
        "          g_arkchemy_bm_w[__i] = ppc_load_u32(ctx, ctx->r[3] + __i * 4u);\n"
        "    }\n"
        "    g_arkchemy_bm_hits++;\n"
        "  }\n")


def main():
    base = pathlib.Path(__file__).resolve().parent.parent / "source"
    for p in sorted(base.glob("generated_*.c")):
        t = p.read_text()
        if SIG not in t:
            continue
        if MARKER in t:
            print("probe_bogus_metaobject: already applied, nothing to do")
            return
        p.write_text(t.replace(SIG, SIG + BODY, 1))
        print("probe_bogus_metaobject: instrumented createInstanceInPlace in %s" % p.name)
        return
    sys.exit("probe_bogus_metaobject: createInstanceInPlace not found -- regenerate first")


if __name__ == "__main__":
    main()
