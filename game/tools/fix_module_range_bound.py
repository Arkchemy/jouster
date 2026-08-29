"""Give the module-range low bound a value that means something here.

Core::igMemoryPoolFrameManager::setMemoryPool asks, for every string it is
about to drop, whether that pointer lives inside the loaded module image:

    217ee90: lwz  r9, 0xc98(r30)   ; A = module start
    217ee98: blt  0x217eea8        ; below the image -> pool memory, release
    217ee9c: lwz  r0, 0xc9c(r29)   ; B = module end
    217eea4: blt  0x217eeb4        ; inside the image -> static literal, KEEP
    217eea8: ...                   ; above the image -> pool memory, release

Ground truth from the real game under Cemu (break on both stores in
Core::igCafeSystemMemory::igCafeSystemMemory, gdb with `set endian big`):

    0x2156938  stw r10, 0xc98(r11)   r10 = 0x02000020   start of .text
    0x2156958  stw r0,  0xc9c(r11)   r0  = 0x10181290   end of the image

so [A, B) is exactly the module image, and the test is "literal or pooled?".

Measured on hardware in the 17:56 build:

    poolrange: A(.bss+306328)=0x183190  B(.bss+306332)=0x70b990

B = 0x70B990 = 7387536 is the synthetic image end and is fine. A is not:
0x183190 = 1585552, and the static data in this build occupies synthetic
[8192, 1585552] -- so A is the TOP of static data, not the bottom. The window
[1585552, 7387536) therefore contains no static data whatsoever, every string
literal compares below A, and all of them take the release path. A literal is
then read as an igStringPoolItem via its "header" at -0xc, which is where
pool == item+0xc comes from and why the boot hangs in igStringPool::remove.

The underlying defect is conquertron's: it folds the symbolic "start of .text"
to a synthetic 1585552 while real text keeps its real addresses (0x2000020
upward), so the module image is not one contiguous range in this address space
and no single [A, B) can describe it. Fixing that properly belongs in the
recompiler and is the follow-up.

What this does meanwhile is pick the window that is correct FOR THIS LAYOUT.
Setting A = 0 gives [0, 7387536):

    static data   synthetic 8192 .. 1585552   -> inside  -> kept    (correct)
    guest heap    0x4000000 upward            -> outside -> released (correct)

Text sits above B and is released, which is harmless: string literals live in
.rodata, not .text.

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

SRC = pathlib.Path(__file__).resolve().parent.parent / "source" / "generated_0156.c"
MARKER = "ARKCHEMY-FIX-MODULE-RANGE-LOW"

OLD = (
    "  /* 2156938: stw r10, 0xc98(r11) */\n"
    "  ppc_store_u32(ctx, ctx->r[11], ctx->r[10]);\n"
)
NEW = (
    "  /* 2156938: stw r10, 0xc98(r11) */\n"
    "  /* " + MARKER + " -- see tools/fix_module_range_bound.py.\n"
    "   * r10 folds to synthetic 1585552, the TOP of static data, so the\n"
    "   * module window would exclude every string literal and release them\n"
    "   * all. 0 makes the window [0, 7387536) cover static data and exclude\n"
    "   * the guest heap, which is what the test means. */\n"
    "  ppc_store_u32(ctx, ctx->r[11], 0u);\n"
)


def main():
    if not SRC.exists():
        sys.exit("missing %s -- regenerate first" % SRC)
    text = SRC.read_text()
    if MARKER in text:
        print("fix_module_range_bound: already applied, nothing to do")
        return
    if text.count(OLD) != 1:
        sys.exit("fix_module_range_bound: expected 1 match at 2156938, found %d"
                 % text.count(OLD))
    SRC.write_text(text.replace(OLD, NEW))
    print("fix_module_range_bound: low bound now 0 in %s" % SRC.name)


if __name__ == "__main__":
    main()
