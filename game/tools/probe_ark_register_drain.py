#!/usr/bin/env python3
"""Record igArkCore::endArkRegister's progress through the class list.

Measured against retail in Cemu on 2026-08-30:

    endArkRegister (0x2154d94)      846+ calls, all from lr=0x215baa4
    igSingleton::userRegister       exactly 1 call

endArkRegister takes one class per call: it reads the index at this->0x40,
compares it against the count at *(this->0x1c), and if the index has caught up
it branches to the drained path -- which is where userRegister is reached. So
retail drains the list exactly once, after 1007-odd classes.

Ours calls userRegister six times, at a dead-regular 58,636 calls apart, and
registration has been stuck at 124 classes for days. Those look like one
problem rather than two: a list that is short, drained, refilled and drained
again. This measures that directly -- the index and the count at every drain,
plus the totals -- which distinguishes "the list is repeatedly refilled" from
"the index is being reset under us".

Usage: tools/probe_ark_register_drain.py [--apply] [source-dir]
"""
import sys, pathlib

MARK = "ARKCHEMY-PROBE-DRAIN"
ANCHOR = """  /* 2154db8: bge 0x2154f28 */
  if ((ctx->cr0_gt || ctx->cr0_eq)) goto L_2154f28;
"""
PROBE = """  /* ARKCHEMY-PROBE-DRAIN -- see tools/probe_ark_register_drain.py.
   * r12 is the index at this->0x40 and r0 the count at *(this->0x1c). Both
   * are recorded on every call, and the first eight drains are kept with the
   * call count: retail drains once, and the shape of ours over time is what
   * says whether the list is refilled or the index reset. */
  { extern unsigned int g_arkchemy_ear_calls, g_arkchemy_ear_idx, g_arkchemy_ear_cnt;
    extern unsigned int g_arkchemy_ear_drains, g_arkchemy_ear_maxidx;
    extern unsigned int g_arkchemy_dr_call[8], g_arkchemy_dr_idx[8], g_arkchemy_dr_cnt[8];
    g_arkchemy_ear_calls++;
    g_arkchemy_ear_idx = ctx->r[12];
    g_arkchemy_ear_cnt = ctx->r[0];
    if (ctx->r[12] > g_arkchemy_ear_maxidx) g_arkchemy_ear_maxidx = ctx->r[12];
    if ((int32_t)ctx->r[12] >= (int32_t)ctx->r[0]) {
      unsigned int __d = g_arkchemy_ear_drains;
      if (__d < 8u) {
        g_arkchemy_dr_call[__d] = (unsigned int)g_ppc_fn_call_count;
        g_arkchemy_dr_idx[__d]  = ctx->r[12];
        g_arkchemy_dr_cnt[__d]  = ctx->r[0];
      }
      g_arkchemy_ear_drains = __d + 1u;
    } }
"""

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    apply = "--apply" in sys.argv
    src = pathlib.Path(args[0] if args else
                       pathlib.Path(__file__).resolve().parent.parent / "source")
    t_path = src / "generated_0157.c"
    if not t_path.exists():
        print(f"no {t_path}", file=sys.stderr); return 1
    t = t_path.read_text()
    if MARK in t:
        print("already applied"); return 0
    if ANCHOR not in t:
        print("anchor not found -- generated code moved", file=sys.stderr); return 1
    t = t.replace(ANCHOR, PROBE + ANCHOR, 1)
    if apply:
        t_path.write_text(t); print("applied: 1 hook")
    else:
        print("would apply: 1 hook\nre-run with --apply to write")
    return 0

sys.exit(main())
