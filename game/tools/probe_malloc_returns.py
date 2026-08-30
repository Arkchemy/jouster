"""Probe: igMemory::mallocAligned is returning static-data addresses.

The corruption is now pinned to an exact instruction. In
igMetaObject::createInstanceInPlace:

    2160dc4: lwz  r0, 4(r31)
    2160dc8: oris r0, r0, 8      ; |= 0x80000
    2160dcc: stw  r0, 4(r31)     ; 0x1 | 0x80000 = 0x80001

Registers captured at the store: r3 = r31 = 0x34D4, and the watched address is
0x34D8, exactly r31+4. The value matches too. So this is an ordinary flag write
on the newly constructed object -- and r31 is `mem`, the element buffer, which
is 0x34D4 = .data+5332. The buffer is static data, so setting a flag on it
lands on the memory-context pointer at .data+5336.

The previous probe tested r3 at entry and found nothing, because r3 is the
METAOBJECT and that is a perfectly good heap pointer (0x4423b64). The wrong
argument was checked. The bad one is r4.

Buffers seen so far -- 0x34D4 and 0x47430 -- are both in static-data space,
well below the MEM2 heap at 0x04000000. They come from
igMemory::mallocAligned, which is the last step before memory is handed back:

    2164aa4: bl igMemory::mallocAligned   (from igPool::allocateBucket)

Records what that function is asked for and what it returns, and counts the
returns that are not heap pointers.

    mallocret: calls=%u bad=%u lastret=0x%x lastsize=%u lastpool=0x%x

  bad > 0                -> confirms the allocator itself is producing
                            static-data addresses, and lastpool says whether
                            that only happens for a particular pool
  bad = 0                -> mallocAligned is fine and the buffer is corrupted
                            between it and createInstanceInPlace

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

MARKER = "ARKCHEMY-PROBE-MALLOCRET"
SIG_PREFIX = "void ppc_mallocAligned__Q2_4Core8igMemoryFUiT1PQ2_4Core12igMemoryPool"
HEAP_BASE = 0x04000000

ENTRY = ("  /* " + MARKER + " entry */\n"
         "  { extern unsigned int g_arkchemy_mr_calls, g_arkchemy_mr_size, g_arkchemy_mr_pool;\n"
         "    g_arkchemy_mr_calls++; g_arkchemy_mr_size = ctx->r[3]; g_arkchemy_mr_pool = ctx->r[6]; }\n")


def main():
    base = pathlib.Path(__file__).resolve().parent.parent / "source"
    for p in sorted(base.glob("generated_*.c")):
        t = p.read_text()
        i = t.find(SIG_PREFIX)
        if i < 0:
            continue
        if MARKER in t:
            print("probe_malloc_returns: already applied, nothing to do")
            return
        brace = t.index("{\n", i) + 2
        t = t[:brace] + ENTRY + t[brace:]
        # and at every return path, classify what is being handed back
        end = t.index("\n}\n", brace)
        body = t[brace:end]
        marked = body.replace(
            "  return;\n",
            "  { extern unsigned int g_arkchemy_mr_bad, g_arkchemy_mr_lastret;\n"
            "    g_arkchemy_mr_lastret = ctx->r[3];\n"
            "    if (ctx->r[3] != 0u && ctx->r[3] < %du) g_arkchemy_mr_bad++; }\n" % HEAP_BASE
            + "  return;\n")
        t = t[:brace] + marked + t[end:]
        p.write_text(t)
        print("probe_malloc_returns: instrumented mallocAligned in %s (%d return sites)"
              % (p.name, body.count("  return;\n")))
        return
    sys.exit("probe_malloc_returns: mallocAligned not found -- regenerate first")


if __name__ == "__main__":
    main()
