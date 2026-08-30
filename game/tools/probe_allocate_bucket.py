#!/usr/bin/env python3
"""Record every igPool::allocateBucket call: pool, size, bucket, buffer.

The polled memory watch (2026-08-30) put the first fault here. The memory
context global at .data+5336 is published correctly at call 3,625 and zeroed
at call 408,510 by igTPool<igHandle::Data>::__constructElement (0x21cf79c),
reached from the bctrl at 0x2164b08 inside allocateBucket. constructElement
writes through the element pointer it is handed, so the element pointer was a
low address -- the write landed on 0x34D8, which is .data+5336 itself.

That pointer is bucket->0xc, filled in by the allocation two instructions
earlier (bl 0x214ae78 / bl 0x214ad58). So the question is what that allocation
returned, and with what pool and size.

allocateBucket is called 17 times in the whole boot, so there is no need to
sample: this records all of them. Two hooks --

  entry (0x2164a24)  the incoming pool (r8) and the caller
  loop head (r27==0) the bucket, the buffer that came back, the element
                     count and element size

Deliberately NOT filtered to the failing call. The 16 healthy calls are the
control: a buffer of 0x34D0 means nothing without knowing what the other
sixteen returned.

Usage: tools/probe_allocate_bucket.py [--apply] [source-dir]
"""
import sys, pathlib

MARK = "ARKCHEMY-PROBE-ALLOCBUCKET"

ENTRY_ANCHOR = "      g_arkchemy_pa2_member = ctx->r[3] ? ppc_load_u32(ctx, ctx->r[3] + 0x20u) : 0u;\n    } }\n"
ENTRY_PROBE = """  /* ARKCHEMY-PROBE-ALLOCBUCKET (entry) -- see tools/probe_allocate_bucket.py.
   * lr is captured here because by the loop head it has been overwritten by
   * the allocation's own bl, and the caller is the thing worth naming. */
  { extern unsigned int g_arkchemy_ab_lr, g_arkchemy_ab_calls;
    g_arkchemy_ab_lr = ctx->lr; g_arkchemy_ab_calls++; }
"""

LOOP_ANCHOR = "  L_2164af0: ;\n"
LOOP_PROBE = """  /* ARKCHEMY-PROBE-ALLOCBUCKET (loop head) -- one entry per call, not per
   * element: r27 is the element index and is zero only on the first pass. */
  if (ctx->r[27] == 0u) {
    extern unsigned int g_arkchemy_ab_n, g_arkchemy_ab_lr;
    extern unsigned int g_arkchemy_ab_call[16], g_arkchemy_ab_buf[16];
    extern unsigned int g_arkchemy_ab_pool[16], g_arkchemy_ab_bucket[16];
    extern unsigned int g_arkchemy_ab_count[16], g_arkchemy_ab_esize[16];
    extern unsigned int g_arkchemy_ab_caller[16];
    unsigned int __n = g_arkchemy_ab_n;
    if (__n < 16u) {
      g_arkchemy_ab_call[__n]   = (unsigned int)g_ppc_fn_call_count;
      g_arkchemy_ab_buf[__n]    = ctx->r[29];
      g_arkchemy_ab_pool[__n]   = ctx->r[31];
      g_arkchemy_ab_bucket[__n] = ctx->r[26];
      g_arkchemy_ab_count[__n]  = ctx->r[25];
      g_arkchemy_ab_esize[__n]  = ctx->r[30];
      g_arkchemy_ab_caller[__n] = g_arkchemy_ab_lr;
    }
    g_arkchemy_ab_n = __n + 1u;
  }
"""

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    apply = "--apply" in sys.argv
    src = pathlib.Path(args[0] if args else
                       pathlib.Path(__file__).resolve().parent.parent / "source")
    target = src / "generated_0158.c"
    if not target.exists():
        print(f"no {target}", file=sys.stderr)
        return 1
    t = target.read_text()
    if MARK in t:
        print("already applied")
        return 0
    for anchor, probe, what in ((ENTRY_ANCHOR, ENTRY_PROBE, "entry"),
                                (LOOP_ANCHOR, LOOP_PROBE, "loop head")):
        if anchor not in t:
            print(f"anchor for {what} not found -- generated code moved", file=sys.stderr)
            return 1
        t = t.replace(anchor, anchor + probe, 1)
    if apply:
        target.write_text(t)
        print("applied: 2 hook(s) in generated_0158.c")
    else:
        print("would apply: 2 hook(s) in generated_0158.c\nre-run with --apply to write")
    return 0

sys.exit(main())
