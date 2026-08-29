"""Probe: are the storage classes' registrations ever entered?

The 20:12 build gave the true registration frontier for the first time (the
earlier "stops at 61" was a hard-coded log window, not the boot). We reach 120
classes; the retail game's own order, captured under Cemu, shows exactly five
classes in its first 125 that we never register:

    igFile                     real #80
    igMemoryStorageDevice      real #98
    igMemoryStorageEntryList   real #99
    igMemoryStorageEntry       real #100
    igVirtualStorageDevice     real #101

and the next one due after our last is igDirectory (real #126). Every one of
them is filesystem or storage. That lines up with two long-standing
observations: our engine opens no file at all in a 120-second run while the
retail game opens /vol/content/alchemy.xml first, and a path was built as
"(null)/" with a NULL base -- which is what a missing storage device would
produce.

All five functions exist in the recompiled output. None is called directly;
each appears only in ppc_dispatch, so registration is driven indirectly
through a table of function pointers. That leaves two very different
situations, and they need different fixes:

    never entered  -> the driver never reaches them. The table entry is
                      missing, or the loop that walks it stops early, and the
                      class itself is irrelevant.
    entered, then
    no registration -> the function runs and fails partway, so the fault is
                      inside it or in what it depends on.

igStorageDevice is instrumented alongside as a control: it registers
successfully and sits at real #83, between the missing ones, so if the driver
were simply stopping early it would be missing too.

Records a bitmask plus per-class entry counts.

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

MARKER = "ARKCHEMY-PROBE-STORAGE-REG"
BASE = pathlib.Path(__file__).resolve().parent.parent / "source"

# (mangled-name fragment, file, bit)
TARGETS = [
    ("6igFile",                  "generated_0162.c", 0),
    ("21igMemoryStorageDevice",  "generated_0162.c", 1),
    ("24igMemoryStorageEntryList", "generated_0162.c", 2),
    ("20igMemoryStorageEntry",   "generated_0162.c", 3),
    ("22igVirtualStorageDevice", "generated_0162.c", 4),
    ("15igStorageDevice",        "generated_0161.c", 5),   # control
]


def main():
    total = 0
    for frag, fname, bit in TARGETS:
        p = BASE / fname
        if not p.exists():
            sys.exit("missing %s" % p)
        t = p.read_text()
        sig = "void ppc_arkRegisterInitialize__Q2_4Core%sSFv(PpcContext *ctx) {\n" % frag
        if sig not in t:
            sys.exit("probe_storage_registration: %s not found in %s" % (frag, fname))
        marked = "/* %s bit %d */" % (MARKER, bit)
        if marked in t:
            continue
        new = (sig
               + "  " + marked + "\n"
               + "  { extern unsigned int g_arkchemy_storagereg_mask, g_arkchemy_storagereg_n[6];\n"
               + "    g_arkchemy_storagereg_mask |= (1u << %d); g_arkchemy_storagereg_n[%d]++; }\n" % (bit, bit))
        t = t.replace(sig, new, 1)
        p.write_text(t)
        total += 1
    print("probe_storage_registration: instrumented %d registration entries" % total)


if __name__ == "__main__":
    main()
