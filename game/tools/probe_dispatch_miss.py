"""Probe: count indirect calls that ppc_dispatch cannot resolve.

ppc_dispatch is a switch over 19,474 function addresses with NO default case.
An address it does not recognise falls out of the switch and returns, so the
indirect call silently does nothing -- no crash, no log, no unhandled
instruction. That is the exact failure mode of the computed-jump-table bug
fixed earlier, where every jump-table switch in the game quietly did nothing.

Why this is suspected now. The registration driver is
__internalObjectBase::getClassMetaSafeInternal, which walks an
__internalFunctionList eight entries at a time and then handles count & 7 in a
remainder tail:

    215b988..215ba2c   main loop, eight bctrl sites, one per entry
    215ba30: clrlwi. r28, r27, 0x1d     ; count & 7
    215ba48: bctrl                       ; remainder
    215ba4c:                             ; <- the return address we recorded

igStorageDevice, the class that DOES register, was dispatched from the
remainder at 0x215ba4c. If the main loop's eight dispatch sites are hitting
addresses ppc_dispatch does not know, those entries silently do nothing, and
exactly this would be observed: some classes register, others never have their
registration function entered at any level, and the ones that work arrive
through a different path.

Records the first unresolved address together with g_ppc_current_pc, plus a
total count.

    dispatchmiss: n=N addr=0x... pc=0x...

  n=0            -> every indirect call resolves; the theory is wrong and the
                    missing classes are absent from the list itself
  n>0            -> indirect calls are silently vanishing, and addr names one
                    of them. Cross-checking that address against the
                    registration functions says whether it is this bug.

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

SRC = pathlib.Path(__file__).resolve().parent.parent / "source" / "generated_0135.c"
MARKER = "ARKCHEMY-PROBE-DISPATCH-MISS"

OLD = ("    case 3506438152u: ppc_import_coreinit_MEMFreeToDefaultHeap(ctx); return;\n"
       "  }\n"
       "}\n")

NEW = ("    case 3506438152u: ppc_import_coreinit_MEMFreeToDefaultHeap(ctx); return;\n"
       "    default: {\n"
       "      /* " + MARKER + " -- see tools/probe_dispatch_miss.py.\n"
       "       * Without this the switch simply falls through and the indirect\n"
       "       * call does nothing at all, silently. */\n"
       "      extern unsigned int g_ppc_dispatch_miss_count;\n"
       "      extern unsigned int g_ppc_dispatch_miss_addr, g_ppc_dispatch_miss_pc;\n"
       "      if (g_ppc_dispatch_miss_count == 0u) {\n"
       "        g_ppc_dispatch_miss_addr = addr;\n"
       "        g_ppc_dispatch_miss_pc   = g_ppc_current_pc;\n"
       "      }\n"
       "      g_ppc_dispatch_miss_count++;\n"
       "      return;\n"
       "    }\n"
       "  }\n"
       "}\n")


def main():
    if not SRC.exists():
        sys.exit("missing %s -- regenerate first" % SRC)
    t = SRC.read_text()
    if MARKER in t:
        print("probe_dispatch_miss: already applied, nothing to do")
        return
    if t.count(OLD) != 1:
        sys.exit("probe_dispatch_miss: expected 1 ppc_dispatch tail, found %d" % t.count(OLD))
    SRC.write_text(t.replace(OLD, NEW))
    print("probe_dispatch_miss: ppc_dispatch now records unresolved addresses")


if __name__ == "__main__":
    main()
