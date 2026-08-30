#!/usr/bin/env python3
"""Run each C++ static initializer at most once, whoever calls it.

Measured on hardware 2026-08-30:

    stiruns: driver=1 ctor=2 heads=[0x0,0x6b640,0x0,0x0]

The harness calls ppc_run_static_initializers exactly once, yet
__sti___26_igCoreMetaSource_cpp runs TWICE -- the second time through
ppc_dispatch, which is the game's own GHS runtime walking the same constructor
table after the harness has already walked it. Both references exist in the
generated C: a direct call from the driver, and a dispatch case at 0x21CB1AC.

That initializer prepends two nodes to the singleton descriptor list, reading
the current head into the second node's next pointer. The heads it read say
the rest: pass 0 read 0, a correct prepend onto an empty list; pass 1 read
0x6b640, a node already in the list, which closes the list into a ring. One
walk of that ring then visits 37,285 nodes instead of 5, reconstructing the
same singletons until the pool is exhausted.

Removing the harness's call is NOT the fix, tempting as it looks. The
bootstrap-heap initialiser is deliberately sequenced after it -- one of the
114 initializers zero-constructs the struct that handle lives in, and an
earlier attempt at running it first was measured failing. Dropping the
harness pass would move every initializer to after that, silently.

C++ static initialization runs once per translation unit on any real target,
so guarding each one is faithful rather than a workaround: whichever caller
arrives first does the work, the other returns immediately, and nothing is
reordered. Counted, so the suppression cannot hide anything -- and the counts
also answer whether the game's own pass covers all 114, which decides whether
the harness call is needed at all.

Usage: tools/guard_static_init_once.py [--apply] [source-dir]
"""
import sys, pathlib, re

MARK = "ARKCHEMY-GUARD-STI-ONCE"
FUNC = re.compile(r'^void (ppc___sti___[A-Za-z0-9_]+)\(PpcContext \*ctx\) \{$', re.M)

GUARD = """  /* ARKCHEMY-GUARD-STI-ONCE -- see tools/guard_static_init_once.py.
   * Both the harness driver and the game's own GHS runtime walk the
   * constructor table, so every initializer here runs twice. C++ static
   * initialization runs once per translation unit on real hardware; the
   * second pass re-prepends to lists that already hold their nodes, which is
   * what closed the singleton list into a ring. */
  { extern unsigned int g_arkchemy_stig_total, g_arkchemy_stig_distinct;
    extern unsigned int g_arkchemy_stig_blocked;
    static unsigned char __ark_sti_done = 0;
    g_arkchemy_stig_total++;
    if (__ark_sti_done) { g_arkchemy_stig_blocked++; return; }
    __ark_sti_done = 1; g_arkchemy_stig_distinct++; }
"""

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    apply = "--apply" in sys.argv
    src = pathlib.Path(args[0] if args else
                       pathlib.Path(__file__).resolve().parent.parent / "source")
    total = 0
    for f in sorted(src.glob("generated_*.c")):
        t = f.read_text()
        if MARK in t:
            print("already applied"); return 0
        out, n = [], 0
        for line in t.splitlines(keepends=True):
            out.append(line)
            if FUNC.match(line.rstrip("\n") + ""):
                out.append(GUARD); n += 1
        if n and apply:
            f.write_text("".join(out))
        total += n
    if total == 0:
        print("no ppc___sti___ functions found", file=sys.stderr); return 1
    print(("applied" if apply else "would apply") + f": {total} initializer guard(s)")
    if not apply: print("re-run with --apply to write")
    return 0

sys.exit(main())
