"""Probe: why does the default-pool lookup return null?

The divergence between our build and the retail game is now a single call.
igPool::activate already contains the fallback, and it is correctly
translated:

    2164bb4: cmpwi r31, 0        ; pool argument null?
    2164bbc: bne   0x2164bd4     ; non-null -> use it
    2164bcc: bl    0x217b058     ; null -> fetch the default pool
    2164bd0: mr    r31, r3
    2164bd8: stw   r31, 0x20(r27); remember it on the igPool
    2164be4: mr    r8, r31       ; hand it to allocateBucket

Measured on both sides:

    retail  allocateBucket receives r8 = 0x116d9314, a real pool
    ours    r8 is NULL

and both receive a null pool at activate's own entry (Cemu confirms retail's
r7=0 there too), so the fallback is the step that differs. 0x217b058 is
Core::igMemoryContext::getMemoryPoolByIndex(int), and in our build it returns
null.

The memory context itself is not the problem -- the harness already reports
cur_mem_ctx = 0x4400170, non-null, every run. So the lookup fails with a valid
context, which means either the index is wrong or the table it indexes is
empty.

Records the call and its result at the one site that matters:

    defpool: hits=%u ctx=0x%x idx=%d ret=0x%x nullret=%u

  ret non-null           -> the fallback works and the null arrives some other
                            way, which would contradict the trace
  ret null, idx sane     -> the pool table is empty at that index, and the
                            question is what fills it
  idx wild               -> the index itself is wrong, which is a different
                            bug and points back at whatever computes it

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

MARKER = "ARKCHEMY-PROBE-DEFPOOL"
OLD = ("  /* 2164bcc: bl 0x217b058 */\n"
       "  ctx->lr = 0x2164bd0u;\n"
       "  ppc_getMemoryPoolByIndex__Q2_4Core15igMemoryContextFi(ctx);\n")
NEW = ("  /* " + MARKER + " -- see tools/probe_default_pool.py */\n"
       "  { extern unsigned int g_arkchemy_dp_ctx, g_arkchemy_dp_idx;\n"
       "    g_arkchemy_dp_ctx = ctx->r[3]; g_arkchemy_dp_idx = ctx->r[4]; }\n"
       + OLD
       + "  { extern unsigned int g_arkchemy_dp_hits, g_arkchemy_dp_ret, g_arkchemy_dp_nullret;\n"
       "    g_arkchemy_dp_hits++;\n"
       "    g_arkchemy_dp_ret = ctx->r[3];\n"
       "    if (ctx->r[3] == 0u) g_arkchemy_dp_nullret++; }\n")


def main():
    base = pathlib.Path(__file__).resolve().parent.parent / "source"
    for p in sorted(base.glob("generated_*.c")):
        t = p.read_text()
        if OLD not in t:
            continue
        if MARKER in t:
            print("probe_default_pool: already applied, nothing to do")
            return
        p.write_text(t.replace(OLD, NEW, 1))
        print("probe_default_pool: instrumented the fallback in %s" % p.name)
        return
    sys.exit("probe_default_pool: fallback call site not found -- regenerate first")


if __name__ == "__main__":
    main()
