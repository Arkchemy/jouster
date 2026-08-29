"""Probe: was this igPool ever activated, or activated with a null pool?

The chain from the null object is now fully traced:

    igMetaObject::createInstanceInPlace  <- mem = NULL, pool = NULL
      igTPool<igHandleRedirect>::constructElement   forwards its own arguments
        igPool::allocateBucket(uint, uint, uint, igMemoryPool*)
          igPool::grow(uint, uint, uint)            has NO pool parameter:
            2164d18: lwz r7, 0x20(r3)   ; pool = this->+0x20   <- NULL
            2164d1c: bl allocateBucket

So grow reads the memory pool from a member at +0x20 of the igPool, and that
member is null.

igPool::activate(uint, uint, uint, igMemoryPool*) is the function that sets
it, and it does so correctly:

    2164b5c: mr  r31, r7           ; the pool argument
    ...      stw r31, 0x20(r27)    ; this->+0x20 = pool

so the store is present and translated. That leaves two possibilities, and
they need different fixes:

    activate never ran for this pool -> grow is being called on an
                                        unactivated igPool, and the question
                                        is what should have activated it
    activate ran with r7 = 0         -> the null comes from ITS caller

This instruments both ends: activate's entry, counting calls and how many
arrive with a null pool, and grow's read of +0x20, recording the igPool whose
member is null along with the caller. Reading lr at ENTRY, where it is still
the true caller.

    poolact: acts=%u nullacts=%u lr=0x%x this=0x%x pool=0x%x
    poolgrow: hits=%u this=0x%x lr=0x%x

If `this` in poolgrow matches an igPool that poolact never saw, the pool was
never activated. If activate saw it with a null argument, nullacts will be
non-zero.

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

MARKER = "ARKCHEMY-PROBE-POOLACT"
ACT_PREFIX = "void ppc_activate__Q2_4Core6igPoolFUiN21PQ2_4Core12igMemoryPool"
GROW_OLD = ("  /* 2164d18: lwz r7, 0x20(r3) */\n"
            "  ctx->r[7] = ppc_load_u32(ctx, ctx->r[3] + (int32_t)32);\n")

ACT_BODY = ("  /* " + MARKER + " -- see tools/probe_pool_activate.py */\n"
            "  { extern unsigned int g_arkchemy_pa_acts, g_arkchemy_pa_nullacts;\n"
            "    extern unsigned int g_arkchemy_pa_lr, g_arkchemy_pa_this, g_arkchemy_pa_pool;\n"
            "    g_arkchemy_pa_acts++;\n"
            "    if (ctx->r[7] == 0u) {\n"
            "      g_arkchemy_pa_nullacts++;\n"
            "      g_arkchemy_pa_lr = ctx->lr;\n"
            "      g_arkchemy_pa_this = ctx->r[3];\n"
            "      g_arkchemy_pa_pool = ctx->r[7];\n"
            "    } }\n")

GROW_NEW = (GROW_OLD
            + "  /* " + MARKER + " (grow side) */\n"
            + "  if (ctx->r[7] == 0u) {\n"
            + "    extern unsigned int g_arkchemy_pg_hits, g_arkchemy_pg_this, g_arkchemy_pg_lr;\n"
            + "    if (g_arkchemy_pg_hits == 0u) { g_arkchemy_pg_this = ctx->r[3]; g_arkchemy_pg_lr = ctx->lr; }\n"
            + "    g_arkchemy_pg_hits++;\n"
            + "  }\n")


def main():
    base = pathlib.Path(__file__).resolve().parent.parent / "source"
    did_act = did_grow = False
    for p in sorted(base.glob("generated_*.c")):
        t = p.read_text()
        changed = False
        if not did_act and ACT_PREFIX in t and MARKER not in t:
            i = t.index(ACT_PREFIX)
            brace = t.index("{\n", i) + 2
            t = t[:brace] + ACT_BODY + t[brace:]
            did_act = changed = True
        if not did_grow and GROW_OLD in t and "(grow side)" not in t:
            t = t.replace(GROW_OLD, GROW_NEW, 1)
            did_grow = changed = True
        if changed:
            p.write_text(t)
    if not (did_act and did_grow):
        sys.exit("probe_pool_activate: activate=%s grow=%s -- regenerate first" % (did_act, did_grow))
    print("probe_pool_activate: instrumented igPool::activate and igPool::grow")


if __name__ == "__main__":
    main()
