"""Probe: where igFile's registration chain stops.

Of the five filesystem classes that never registered, four now do. Applying
data relocations brought back igMemoryStorageDevice, igMemoryStorageEntryList
and igMemoryStorageEntry; converting bctr jump tables brought back
igVirtualStorageDevice and took registration from 99 classes to 124. igFile
(retail #80) is the last one, and the next class due overall is igDirectory
(#126), which is also filesystem.

Each class registers through three levels, every one reachable only through
ppc_dispatch:

    arkRegister_X  ->  arkRegisterInternal_X  ->  arkRegisterInitialize_X

An earlier version of this probe was applied inline and lost on the first
regeneration, which is why it is a script now. It instruments all three levels
for igFile, with igStorageDevice -- which registers successfully and sits at
retail #83, inside the range igFile belongs to -- as a control.

    igfile: mask=0x%02x
      bit0 arkRegister(igFile)            bit3 arkRegister(igStorageDevice)
      bit1 arkRegisterInternal(igFile)    bit4 arkRegisterInternal(igStorageDevice)
      bit2 arkRegisterInitialize(igFile)  bit5 arkRegisterInitialize(igStorageDevice)

Reading it:
  bits 0-2 clear, 3-5 set -> igFile is never dispatched to at any level, so
                             its entry is missing from whatever list the
                             driver walks, and the class itself is irrelevant
  bit 0 set, 1 clear      -> arkRegister runs and fails before reaching
                             Internal
  bits 0-2 set            -> the whole chain runs and the failure is after it,
                             in appendToArkCore or what it depends on

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

MARKER = "ARKCHEMY-PROBE-IGFILE"
TARGETS = [
    ("arkRegister",           "6igFile",           0),
    ("arkRegisterInternal",   "6igFile",           1),
    ("arkRegisterInitialize", "6igFile",           2),
    ("arkRegister",           "15igStorageDevice", 3),
    ("arkRegisterInternal",   "15igStorageDevice", 4),
    ("arkRegisterInitialize", "15igStorageDevice", 5),
]


def main():
    base = pathlib.Path(__file__).resolve().parent.parent / "source"
    files = sorted(base.glob("generated_*.c"))
    done = 0
    for kind, frag, bit in TARGETS:
        sig = "void ppc_%s__Q2_4Core%sSFv(PpcContext *ctx) {\n" % (kind, frag)
        for p in files:
            t = p.read_text()
            if sig not in t:
                continue
            if "%s bit %d" % (MARKER, bit) in t:
                done += 1
                break
            body = (sig
                    + "  /* %s bit %d -- see tools/probe_igfile_registration.py */\n" % (MARKER, bit)
                    + "  { extern unsigned int g_arkchemy_igfile_mask;\n"
                    + "    g_arkchemy_igfile_mask |= (1u << %d); }\n" % bit)
            p.write_text(t.replace(sig, body, 1))
            done += 1
            break
    if done != len(TARGETS):
        sys.exit("probe_igfile_registration: instrumented %d of %d -- regenerate first"
                 % (done, len(TARGETS)))
    print("probe_igfile_registration: instrumented %d registration entries" % done)


if __name__ == "__main__":
    main()


# ---------------------------------------------------------------------------
# Second site: the driver itself.
#
# mask=0x38 on hardware -- igStorageDevice's whole chain runs, igFile's is
# never entered at any level. So the question is no longer what igFile does,
# it is whether the driver ever sees it.
#
# Core::__internalObjectBase::getClassMetaSafeInternal walks a function-pointer
# list, unrolled eight at a time with a count & 7 remainder tail, and every
# entry goes through ppc_dispatch. igFile's arkRegister address is written into
# .rodata exactly once by a data relocation; igStorageDevice's appears three
# times, because its subclasses' tables reference the base too.
#
# This counts every pointer the driver dispatches and flags whether igFile's
# address is ever among them. That separates "the list does not contain it"
# from "the list contains it and the dispatch fails".
MARKER2 = "ARKCHEMY-PROBE-IGFILE-DRIVER"
DRIVER_SIG = "void ppc_getClassMetaSafeInternal__Q2_4Core20__internalObjectBaseSFRPQ2_4Core12igMetaObjectPFv_PQ2_4Core22__internalFunctionList(PpcContext *ctx) {\n"
IGFILE_ARKREGISTER = 35335860  # 0x21b2eb4


def patch_driver():
    base = pathlib.Path(__file__).resolve().parent.parent / "source"
    for p in sorted(base.glob("generated_*.c")):
        t = p.read_text()
        if DRIVER_SIG not in t:
            continue
        if MARKER2 in t:
            print("probe_igfile_registration: driver already instrumented")
            return
        # count and inspect every dispatch this function makes
        old = "  ppc_dispatch(ctx, ctx->ctr);\n"
        new = ("  { extern unsigned int g_arkchemy_drv_dispatches, g_arkchemy_drv_saw_igfile;\n"
               "    g_arkchemy_drv_dispatches++;\n"
               "    if (ctx->ctr == %du) g_arkchemy_drv_saw_igfile++; }\n" % IGFILE_ARKREGISTER
               + "  ppc_dispatch(ctx, ctx->ctr);\n")
        start = t.index(DRIVER_SIG)
        end = t.index("\n}\n", start)
        body = t[start:end].replace(old, new)
        t = t[:start] + "/* " + MARKER2 + " */\n" + body + t[end:]
        p.write_text(t)
        print("probe_igfile_registration: instrumented the driver in %s" % p.name)
        return
    sys.exit("probe_igfile_registration: driver not found")


patch_driver()
