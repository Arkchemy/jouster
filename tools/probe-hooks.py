#!/usr/bin/env python3
"""Put the diagnostic probe calls into the generated C.

conquertron's runtime defines the probes (ark_pump, ark_rel_gate, the
g_ark_bs counters, ...) and main.c prints what they collect, but the calls
themselves have to sit inside the recompiled game functions. Until
2026-09-28 they were patched into the generated C by hand on the Windows
build machine and never committed, so every fresh regenerate produced a
build whose TALLY read reads=0 tasks=0 and whose BOOTSTRAP, ARCHDRIVE and
ARCHPUMP lines read zero while the game was loading fine. This script is
that patch, written down.

Two kinds of site:

  ENTRY  after the function's prologue bookkeeping (the line that sets
         g_ppc_current_pc), so it runs on every call, direct or dispatched.
  AFTER  after the C for the instruction at an address, before the next
         instruction. Only for instructions that fall through (loads, not
         branches), or the probe would miss the taken path.

Every site must be found exactly once, or this exits non-zero: a probe that
silently goes missing reads as "never called", which is the mistake this
exists to stop. Idempotent -- each inserted line carries a marker and a
second run changes nothing.

regenerate.sh runs this after native-overrides.py. By hand:

    python3 tools/probe-hooks.py [game-dir]
"""
import glob
import os
import re
import sys

MARK = "/* ark-probe */"

# BOOTSTRAP: g_ark_bs[i] = {calls, first call index, last call index}.
def bs(i):
    return ("{ volatile uint32_t *b_ = g_ark_bs[%d]; if (!b_[0]) b_[1] = (uint32_t)g_ppc_fn_call_count;"
            " b_[0]++; b_[2] = (uint32_t)g_ppc_fn_call_count; }" % i)

ENTRY = {
    # ARCHPUMP slots, see g_ark_ap in ppc_runtime.h; 14 and 15 are ARCHDRIVE's
    "ppc_updateArchiveSystem__Q2_4Core9igArchiveSFv": "ark_pump(0);",
    "ppc_updateTasks__Q2_4Core9igArchiveSFv": "ark_pump(1);",
    "ppc_startNewTasks__Q2_4Core9igArchiveSFv": "ark_pump(2);",
    "ppc_startBlockRead__Q2_4Core9igArchiveSFPQ2_4Core16igFileDescriptorPvUiT3": "ark_pump(3);",
    "ppc_decompressBatch__4CoreFPQ3_4Core10igJobQueue5Batch": "ark_pump(4);",
    "ppc_allocate__Q2_4Core21igArchiveBlockManagerFv": "ark_pump(5);",
    "ppc_getNumAvailableBlocks__Q2_4Core21igArchiveBlockManagerFv": "ark_pump(7);",
    "ppc_addWork__Q2_4Core9igArchiveFPQ2_4Core14igFileWorkItemQ2_4Core14igBlockingType": "ark_pump(9);",
    "ppc_update__Q2_4Core9igArchiveFQ2_4Core14igBlockingType": "ark_pump(14);",
    "ppc_update__Q2_4Core13igFileContextFv": "ark_pump(15);",
    # BOOTSTRAP, in main.c's order
    "ppc_bootstrapInitialize__Q2_4Core15igMemoryContextFv": bs(0),
    "ppc_bootstrapUninitialize__Q2_4Core15igMemoryContextFv": bs(1),
    "ppc_initBootstrap__Q2_4Core9igArkCoreFv": bs(2),
    "ppc_exitBootstrap__Q2_4Core9igArkCoreFv": bs(3),
    "ppc_bootstrapInitialize__Q2_4Core12igStringPoolSFv": bs(4),
    "ppc_bootstrapUninitialize__Q2_4Core12igStringPoolSFv": bs(5),
}

AFTER = {
    # RELGATE (TALLY's tasks): igArchive::updateTasks loads task->_block
    # (+0x1c) into r21 from the task in r16; null means the block is not
    # handed back. See ark_rel_gate in ark_blockprobe.h.
    0x21682a0: "ark_rel_gate(ctx->r[16], ctx->r[21]);",
}

def main():
    game = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "game")
    files = sorted(glob.glob(os.path.join(game, "source", "generated_*.c")))
    if not files:
        sys.exit("probe-hooks: no generated_*.c under %s -- run regenerate.sh first" % game)

    def_re = re.compile(r'^void (ppc_[A-Za-z0-9_]+)\(PpcContext \*ctx\) \{$')
    insn_re = re.compile(r'^\s*/\* ([0-9a-f]+): ')
    found = {k: 0 for k in ENTRY}
    found.update({k: 0 for k in AFTER})
    added = 0

    for path in files:
        with open(path) as f:
            lines = f.readlines()
        out, changed = [], False
        pending_entry = None      # hook waiting for the prologue line
        pending_after = None      # hook waiting for the next instruction
        for i, line in enumerate(lines):
            m = def_re.match(line)
            if m:
                pending_after = None
                if m.group(1) in ENTRY:
                    found[m.group(1)] += 1
                    pending_entry = ENTRY[m.group(1)]
                out.append(line)
                continue
            im = insn_re.match(line)
            if pending_after is not None and (im or line.startswith("}") or re.match(r'^\s*L_[0-9a-f]+: ;', line)):
                if MARK not in out[-1]:
                    out.append("  %s %s\n" % (pending_after, MARK)); changed = True; added += 1
                pending_after = None
            out.append(line)
            if pending_entry is not None and "g_ppc_current_pc =" in line:
                nxt = lines[i + 1] if i + 1 < len(lines) else ""
                if MARK not in nxt:
                    out.append("  %s %s\n" % (pending_entry, MARK)); changed = True; added += 1
                pending_entry = None
            if im and int(im.group(1), 16) in AFTER:
                found[int(im.group(1), 16)] += 1
                pending_after = AFTER[int(im.group(1), 16)]
        # ark_blockprobe.h is kept out of ppc_runtime.h so that touching it
        # rebuilds two objects, not 224 -- include it only where a probe sits
        inc = '#include "ark_blockprobe.h"\n'
        if any(MARK in l for l in out) and inc not in out:
            at = out.index('#include "ppc_runtime.h"\n') + 1
            out.insert(at, inc); changed = True
        if changed:
            with open(path, "w") as f:
                f.writelines(out)

    bad = {k: n for k, n in found.items() if n != 1}
    if bad:
        for k, n in bad.items():
            name = k if isinstance(k, str) else "instruction 0x%x" % k
            print("probe-hooks: %s found %d times, expected 1" % (name, n), file=sys.stderr)
        sys.exit(1)
    print("probe hooks: %d site(s), %d inserted this run" % (len(found), added))

if __name__ == "__main__":
    main()
