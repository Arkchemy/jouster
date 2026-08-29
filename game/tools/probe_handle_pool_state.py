"""Probe: what does a freshly instantiated igHandleRedirectPool look like?

The chain behind the null virtual calls is now traced end to end, and the
bottom of it is not where it appeared to be:

    igObjectHandleManager::userInstantiate(bool)
      21501d4: bl igDataList::setCapacity
      21501e4: bl igHandleRedirectPool::instantiateFromPool
      21501e8: stw r3, 0x2c(r28)
      21501ec: lwz r8, 0x18(r3)     ; the new pool's +0x18
      21501f0: cmpwi r8, 0
      21501f4: bne 0x2150220        ; non-zero -> the normal path
      2150204: li r7, 0             ; else pool = 0, a HARDCODED literal
      2150208: bl igPool::activate

Measured: acts=17, nullacts=17 -- every single call to activate arrives with a
null pool, and poolgrow hits=0, so grow was never involved despite being where
the earlier trace pointed.

Passing null to activate is therefore not a bug. It is the game's own code, on
a branch that is taken only when the freshly created pool's +0x18 reads zero.
The real game takes the other branch. So the fault is that +0x18 is zero on an
object that was just instantiated -- and the call immediately before it is
igDataList::setCapacity, the same function whose type-index lookup was broken
earlier today.

Captures the object right where the branch reads it: eight words of the new
igHandleRedirectPool, plus the igMemoryPool passed to instantiateFromPool.

    hpool: hits=%u obj=0x%x pool=0x%x w=[0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]

  w[6] is +0x18, the word the branch tests.

  all words zero        -> the object was allocated and never constructed
  some set, +0x18 zero  -> constructed but that field never written, which
                           points at what should write it
  obj = 0               -> instantiateFromPool returned nothing at all

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

MARKER = "ARKCHEMY-PROBE-HPOOL"
# userInstantiate contains TWO structurally identical copies of this test,
# at 0x2150198 and 0x21501ec, on different branches of its bool argument.
# The first attempt instrumented only 0x21501ec -- the one the earlier trace
# pointed at -- and it never executed: hits=0 while activate was still being
# called 17 times, because control reaches the null-pool path through
# `beq 0x21501f8` from the OTHER copy. Both are instrumented now.
SITES = ["2150198", "21501ec"]
BODY = ("  /* " + MARKER + " -- see tools/probe_handle_pool_state.py */\n"
       + "  { extern unsigned int g_arkchemy_hp_hits, g_arkchemy_hp_obj, g_arkchemy_hp_pool;\n"
       + "    extern unsigned int g_arkchemy_hp_w[8];\n"
       + "    if (g_arkchemy_hp_hits == 0u) {\n"
       + "      unsigned int __i;\n"
       + "      g_arkchemy_hp_obj = ctx->r[3];\n"
       + "      g_arkchemy_hp_pool = ctx->r[29];\n"
       + "      if (ctx->r[3])\n"
       + "        for (__i = 0; __i < 8u; __i++)\n"
       + "          g_arkchemy_hp_w[__i] = ppc_load_u32(ctx, ctx->r[3] + __i * 4u);\n"
       + "    }\n"
       + "    g_arkchemy_hp_hits++; }\n")


def main():
    base = pathlib.Path(__file__).resolve().parent.parent / "source"
    for p in sorted(base.glob("generated_*.c")):
        t = p.read_text()
        if MARKER in t:
            print("probe_handle_pool_state: already applied, nothing to do")
            return
        n = 0
        for site in SITES:
            old = ("  /* %s: lwz r8, 0x18(r3) */\n"
                   "  ctx->r[8] = ppc_load_u32(ctx, ctx->r[3] + (int32_t)24);\n" % site)
            if old not in t:
                continue
            t = t.replace(old, old + BODY, 1)
            n += 1
        if n:
            p.write_text(t)
            print("probe_handle_pool_state: instrumented %d of %d +0x18 tests in %s"
                  % (n, len(SITES), p.name))
            return
    sys.exit("probe_handle_pool_state: site not found -- regenerate first")


if __name__ == "__main__":
    main()
