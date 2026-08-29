"""Probe: which type's pool hands out a null element buffer?

With the dispatch miss finally recording ctx->lr -- the address the bctrl
itself writes, rather than a function-entry lr that goes stale -- the true call
site is 0x2160d84 in Core::igMetaObject::createInstanceInPlace:

    2160d78: mtctr r8       ; a vtable slot
    2160d7c: mr    r3, r31  ; this = the new object
    2160d80: li    r4, 1
    2160d84: bctrl          ; virtual call with arg 1

this=0, and r31 comes straight from the function's own second argument:

    2160cc0: mr r31, r4     ; createInstanceInPlace(meta, void* mem, pool)

So `mem` is null. The object is not failing to construct -- there is nowhere
to construct it. All 27 callers are igTPool<T>::constructElement(unsigned
char*, igMemoryPool*) template instantiations, so a pool is handing out a null
element buffer.

Instrumenting 27 call sites would be the obvious move and the wrong one: they
all funnel through this single function, whose first argument is the
metaobject, whose name pointer is at +0x8 -- the same offset the registration
log reads. One patch names the type.

    nullinst: hits=%u lr=0x%x meta=0x%x pool=0x%x name="%s"

  lr    the specific constructElement instantiation, so the T is identifiable
  pool  the igMemoryPool that produced nothing
  name  the class whose instance could not be placed

Note this is separate from MEMAllocFromExpHeap: the harness reports mem fail=0,
so nothing failed at the Cafe OS heap layer. This is the engine's own pool
allocator returning null, which is a different thing.

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

MARKER = "ARKCHEMY-PROBE-NULLINST"
SIG = "void ppc_createInstanceInPlace__Q2_4Core12igMetaObjectCFPvPQ2_4Core12igMemoryPool(PpcContext *ctx) {\n"

BODY = ("  /* " + MARKER + " -- see tools/probe_null_instance_buffer.py */\n"
        "  if (ctx->r[4] == 0u) {\n"
        "    extern unsigned int g_arkchemy_ni_hits, g_arkchemy_ni_lr, g_arkchemy_ni_meta, g_arkchemy_ni_pool;\n"
        "    extern char g_arkchemy_ni_name[64];\n"
        "    if (g_arkchemy_ni_hits == 0u) {\n"
        "      unsigned int __i, __np;\n"
        "      g_arkchemy_ni_lr = ctx->lr;\n"
        "      g_arkchemy_ni_meta = ctx->r[3];\n"
        "      g_arkchemy_ni_pool = ctx->r[5];\n"
        "      __np = ctx->r[3] ? ppc_load_u32(ctx, ctx->r[3] + 0x8u) : 0u;\n"
        "      if (__np) {\n"
        "        for (__i = 0; __i < 63u; __i++) {\n"
        "          unsigned char __c = (unsigned char)ppc_load_u8(ctx, __np + __i);\n"
        "          if (__c < 32u || __c > 126u) break;\n"
        "          g_arkchemy_ni_name[__i] = (char)__c;\n"
        "        }\n"
        "      }\n"
        "    }\n"
        "    g_arkchemy_ni_hits++;\n"
        "  }\n")


def main():
    base = pathlib.Path(__file__).resolve().parent.parent / "source"
    for p in sorted(base.glob("generated_*.c")):
        t = p.read_text()
        if SIG not in t:
            continue
        if MARKER in t:
            print("probe_null_instance_buffer: already applied, nothing to do")
            return
        p.write_text(t.replace(SIG, SIG + BODY, 1))
        print("probe_null_instance_buffer: instrumented createInstanceInPlace in %s" % p.name)
        return
    sys.exit("probe_null_instance_buffer: createInstanceInPlace not found -- regenerate first")


if __name__ == "__main__":
    main()
