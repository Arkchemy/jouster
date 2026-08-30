#!/usr/bin/env python3
"""Record who asks for igObjectHandleManager, and how often.

Measured against retail in Cemu on 2026-08-30: igPool::activate allocates the
6625x24 / 64x12 / 18000x12 bucket triple exactly ONCE, from three singleton
constructors each called exactly once --

    igObjectHandleManager::userInstantiate  (0x214ff78)  1 call, lr=0x215bec0
    igMemoryHandleContext::userInstantiate  (0x214ea3c)  1 call, lr=0x215bec0

Ours runs the same triple SIX times, with identical counts and element sizes,
which is what exhausts the 5MB pool. Same work, same shapes, six times over.

0x215bec0 is inside igMetaObject::constructInstance, which is generic -- it
constructs whatever it is handed. So the repetition is a level above it, in
whatever asks constructInstance for this class, and that is what this records:
for each call to the singleton constructor, its own caller, and the lr and
metaobject of the constructInstance frame that reached it.

Six identical grandparents means one guard that never takes. Six different
ones means six separate requesters, which is a different problem entirely --
and the two are indistinguishable from any counter.

Usage: tools/probe_singleton_repeat.py [--apply] [source-dir]
"""
import sys, pathlib

MARK = "ARKCHEMY-PROBE-SINGLETON"

CI_ANCHOR = "  g_ppc_current_pc = 0x215bcb0u; g_ppc_fn_call_count++;\n"
CI_PROBE = """  /* ARKCHEMY-PROBE-SINGLETON (constructInstance) -- the frame that reaches
   * the singleton constructor, kept so the callee can name its grandparent.
   * ctx->lr at entry is the only caller information that survives, and one
   * level is not enough here: the immediate caller is the same generic site
   * for every class. */
  { extern unsigned int g_arkchemy_ci_lr, g_arkchemy_ci_this;
    g_arkchemy_ci_lr = ctx->lr; g_arkchemy_ci_this = ctx->r[3]; }
"""

TARGETS = {
    "0x214ff78u": ("ohm", "igObjectHandleManager::userInstantiate"),
    "0x214ea3cu": ("mhc", "igMemoryHandleContext::userInstantiate"),
}

def probe_for(tag, name):
    return f"""  /* ARKCHEMY-PROBE-SINGLETON ({tag}) -- {name}.
   * Retail calls this exactly once. */
  {{ extern unsigned int g_arkchemy_{tag}_n;
    extern unsigned int g_arkchemy_{tag}_call[8], g_arkchemy_{tag}_lr[8];
    extern unsigned int g_arkchemy_{tag}_gp[8], g_arkchemy_{tag}_meta[8];
    extern unsigned int g_arkchemy_ci_lr, g_arkchemy_ci_this;
    unsigned int __n = g_arkchemy_{tag}_n;
    if (__n < 8u) {{
      g_arkchemy_{tag}_call[__n] = (unsigned int)g_ppc_fn_call_count;
      g_arkchemy_{tag}_lr[__n]   = ctx->lr;
      g_arkchemy_{tag}_gp[__n]   = g_arkchemy_ci_lr;
      g_arkchemy_{tag}_meta[__n] = g_arkchemy_ci_this;
    }}
    g_arkchemy_{tag}_n = __n + 1u;
  }}
"""

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    apply = "--apply" in sys.argv
    src = pathlib.Path(args[0] if args else
                       pathlib.Path(__file__).resolve().parent.parent / "source")
    hits = 0
    for f in sorted(src.glob("generated_*.c")):
        t = f.read_text()
        if MARK in t:
            print("already applied"); return 0
        orig = t
        if CI_ANCHOR in t:
            t = t.replace(CI_ANCHOR, CI_ANCHOR + CI_PROBE, 1); hits += 1
        for pc, (tag, name) in TARGETS.items():
            a = f"  g_ppc_current_pc = {pc}; g_ppc_fn_call_count++;\n"
            if a in t:
                t = t.replace(a, a + probe_for(tag, name), 1); hits += 1
        if t != orig and apply:
            f.write_text(t)
    if hits != 3:
        print(f"expected 3 hook sites, found {hits} -- generated code moved", file=sys.stderr)
        return 1
    print(("applied" if apply else "would apply") + ": 3 hook(s)")
    if not apply: print("re-run with --apply to write")
    return 0

sys.exit(main())
