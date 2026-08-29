"""Probe: what stops registration at the frontier?

A correction first. igFile was treated as a missing class for several rounds.
It is not: arkRegisterInternal(igIGBFile) owns the only dependency list that
contains igFile's arkRegister, and igIGBFile is retail #127 while we reach
#125. Registration is recursive -- a class's dependencies fire before it does,
which is why retail logs igFile at #80 while its owner appears at #127. igFile
is simply beyond the frontier, not skipped.

That also undermines "our lists are short": maxlist=11 against a retail 21 is
expected if the classes with longer lists are ones we have not reached.

So the real question is the frontier itself. Retail order:

    #125 igInternalLockableHandleMemoryPool   <- our last
    #126 igDirectory
    #127 igIGBFile                            <- would pull in igFile
    #128 igDirEntry

This instruments igDirectory's registration chain, with igIGBFile alongside,
so the next run says whether the frontier is reached and fails, or never
reached at all.

    frontier: mask=0x%02x
      bit0 arkRegister(igDirectory)      bit3 arkRegister(igIGBFile)
      bit1 arkRegisterInternal(igDirectory)  bit4 arkRegisterInternal(igIGBFile)
      bit2 arkRegisterInitialize(igDirectory) bit5 arkRegisterInitialize(igIGBFile)

  all clear      -> nothing asks for either class; registration has simply run
                    out of things to do, and the question is what SHOULD ask
  bits 0-2 set   -> igDirectory runs; the frontier has moved and something
                    after it fails
  bit 0 set only -> it is entered and fails inside

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

MARKER = "ARKCHEMY-PROBE-FRONTIER"
TARGETS = [
    ("arkRegister",           "11igDirectory", 0),
    ("arkRegisterInternal",   "11igDirectory", 1),
    ("arkRegisterInitialize", "11igDirectory", 2),
    ("arkRegister",           "9igIGBFile",    3),
    ("arkRegisterInternal",   "9igIGBFile",    4),
    ("arkRegisterInitialize", "9igIGBFile",    5),
]


def main():
    base = pathlib.Path(__file__).resolve().parent.parent / "source"
    files = sorted(base.glob("generated_*.c"))
    done = missing = 0
    for kind, frag, bit in TARGETS:
        sig = "void ppc_%s__Q2_4Core%sSFv(PpcContext *ctx) {\n" % (kind, frag)
        hit = False
        for p in files:
            t = p.read_text()
            if sig not in t:
                continue
            hit = True
            if "%s bit %d" % (MARKER, bit) in t:
                done += 1
                break
            body = (sig
                    + "  /* %s bit %d -- see tools/probe_frontier.py */\n" % (MARKER, bit)
                    + "  { extern unsigned int g_arkchemy_frontier_mask;\n"
                    + "    g_arkchemy_frontier_mask |= (1u << %d); }\n" % bit)
            p.write_text(t.replace(sig, body, 1))
            done += 1
            break
        if not hit:
            missing += 1
            print("  note: %s__%s not present in the binary" % (kind, frag))
    print("probe_frontier: instrumented %d entries (%d absent)" % (done, missing))


if __name__ == "__main__":
    main()
