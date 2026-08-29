"""Probe: who calls igTPool<igHandleRedirect>::constructElement with nulls?

The 23:31 run named the type outright:

    nullinst: hits=1 lr=0x21cfbf8 meta=0x4423b64 pool=0x0 name="igHandleRedirect"

pool=0 as well as the buffer, so createInstanceInPlace is handed a null
element buffer AND a null igMemoryPool. Both come straight through
igTPool<igHandleRedirect>::constructElement, which passes its own arguments on:

    21cfbe8: mr r5, r29   ; pool
    21cfbec: mr r4, r30   ; buffer
    21cfbf0: mr r3, r31   ; metaobject
    21cfbf4: bl createInstanceInPlace

igHandleRedirect is retail #121 and we register it, so this is not a class
beyond the frontier -- it is one we have, whose pool was never set up. Its pool
class, igHandleRedirectPool, is #120.

constructElement has no direct callers anywhere in the output; it is reached
through a function pointer, so the caller cannot be found statically.

ctx->lr at FUNCTION ENTRY is the true caller's return address -- the same value
g_ppc_last_caller_lr is initialised from, before anything downstream makes it
stale. That distinction is what cost four wrong call sites earlier today, so it
is worth stating: reading lr at entry is correct, reading it later is not.

    ctorelem: hits=%u lr=0x%x this=0x%x buf=0x%x pool=0x%x

  lr    the caller, which is the thing that could not be found statically
  this  the igTPool itself, whose state says whether the pool was ever built
  buf
  pool  both expected null on the failing call

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

MARKER = "ARKCHEMY-PROBE-CTORELEM"
# GHS appends a source-path suffix to statics, so match by prefix rather than
# by the full mangled name -- which is also why the first attempt failed.
SIG_PREFIX = "void ppc___constructElement__Q2_4Core42igTPool__tm__27_Q2_4Core16igHandleRedirect"

BODY = ("  /* " + MARKER + " -- see tools/probe_construct_element.py */\n"
        "  { extern unsigned int g_arkchemy_ce_hits, g_arkchemy_ce_lr, g_arkchemy_ce_this;\n"
        "    extern unsigned int g_arkchemy_ce_buf, g_arkchemy_ce_pool;\n"
        "    if (g_arkchemy_ce_hits == 0u || ctx->r[4] == 0u || ctx->r[5] == 0u) {\n"
        "      g_arkchemy_ce_lr = ctx->lr;\n"
        "      g_arkchemy_ce_this = ctx->r[3];\n"
        "      g_arkchemy_ce_buf = ctx->r[4];\n"
        "      g_arkchemy_ce_pool = ctx->r[5];\n"
        "    }\n"
        "    g_arkchemy_ce_hits++; }\n")


def main():
    base = pathlib.Path(__file__).resolve().parent.parent / "source"
    for p in sorted(base.glob("generated_*.c")):
        t = p.read_text()
        idx = t.find(SIG_PREFIX)
        if idx < 0:
            continue
        if MARKER in t:
            print("probe_construct_element: already applied, nothing to do")
            return
        brace = t.index("{\n", idx) + 2
        p.write_text(t[:brace] + BODY + t[brace:])
        print("probe_construct_element: instrumented constructElement in %s" % p.name)
        return
    sys.exit("probe_construct_element: constructElement not found -- regenerate first")


if __name__ == "__main__":
    main()
