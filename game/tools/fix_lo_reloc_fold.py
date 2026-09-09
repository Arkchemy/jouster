#!/usr/bin/env python3
"""Applies the R_PPC_ADDR16_LO fold to generated C that a stale `recomp`
missed, for the D-form load/stores whose codegen.cpp handlers were fixed
on 2026-09-09.

Background: when a `lis` carries R_PPC_ADDR16_HA, conquertron writes the
*complete* relocated address into the base register, so the consuming
instruction's own 16-bit displacement is a relocation placeholder and must
NOT be added on top of it. Eleven D-form handlers (lbz/lhz/lha/stb/sth,
lfsu/lfdu/stfsu/stfdu, lmw/stmw) were adding it anyway, so every byte,
halfword, float-with-update and multiple-word access to a relocated global
read or wrote the wrong address. The u32 path was always correct, which is
why this survived so long.

That is fixed in conquertron/src/codegen.cpp; this script exists only
because `recomp` cannot be rebuilt on a machine with no C++ compiler and
no capstone headers (see conquertron/build_with_zig.sh's own comment on
exactly this trap). It reads the same .rela.text the recompiler reads and
applies the same rule, so the output is what a rebuilt recomp emits.
Delete it once regenerate.sh has been re-run with a fixed recomp.

Usage: fix_lo_reloc_fold.py <tfbGame_cafe.rpx> <source-dir>
"""
import re
import struct
import sys
import glob
import os

# Instructions whose handler was missing the fold. Anything not listed
# here already folded correctly and must be left alone.
UPDATE_FORM = {"lfsu", "lfdu", "stfsu", "stfdu"}
PLAIN_FORM = {"lbz", "lhz", "lha", "stb", "sth"}
# lmw/stmw were fixed in codegen.cpp too, but this binary has zero of them
# at an LO-reloc site, and their emitted form folds the displacement into
# a per-register offset that this script would have to unpick arithmetically
# rather than just delete. Left out deliberately: nothing to fix here, and
# a rebuilt recomp covers them.
ORI_FORM = {"ori"}
TARGETS = UPDATE_FORM | PLAIN_FORM | ORI_FORM

R_PPC_ADDR16_LO = 4

COMMENT = re.compile(r"^\s*/\* ([0-9a-f]{6,8}): ([a-z][a-z0-9_.]*)\s")
# `ctx->r[N] + (int32_t)DISP` as it appears inside a ppc_load_*/ppc_store_*
DISP = re.compile(r"(ctx->r\[\d+\]) \+ \(int32_t\)-?\d+")
WRITEBACK = re.compile(r"^\s*(ctx->r\[\d+\]) = \1 \+ \(int32_t\)-?\d+;\s*$")
ORI = re.compile(r"(= ctx->r\[\d+\]) \| \d+u;\s*$")


def lo_reloc_addrs(rpx_path):
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "blaster"))
    import rpx as rpxmod
    r = rpxmod.Rpx(rpx_path)
    sec = r.section(".rela.text")
    if not sec:
        sys.exit("no .rela.text in " + rpx_path)
    d = sec["data"]
    out = set()
    for o in range(0, len(d) - 11, 12):
        off, info, _add = struct.unpack_from(">IIi", d, o)
        if (info & 0xFF) == R_PPC_ADDR16_LO:
            # ADDR16_* point at the low halfword, two bytes into the
            # instruction word; align down to the instruction's address.
            out.add(off & ~3)
    return out


def fix_file(path, lo):
    lines = open(path).read().split("\n")
    out = []
    i = 0
    n = len(lines)
    changed = 0
    while i < n:
        line = lines[i]
        out.append(line)
        i += 1
        m = COMMENT.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        mnem = m.group(2)
        if mnem not in TARGETS or addr not in lo:
            continue
        # Emitted body runs until the next disassembly comment.
        body = []
        while i < n and not COMMENT.match(lines[i]):
            body.append(lines[i])
            i += 1
        new = []
        for b in body:
            if mnem in ORI_FORM:
                # lis+ori address pair: rA already holds the full address,
                # the immediate is a placeholder.
                nb = ORI.sub(r"\1;", b)
                if nb != b:
                    changed += 1
                new.append(nb)
                continue
            if mnem in UPDATE_FORM and WRITEBACK.match(b):
                # Base register already holds the final EA; the writeback
                # would advance it past the object.
                changed += 1
                continue
            nb = DISP.sub(r"\1", b)
            if nb != b:
                changed += 1
            new.append(nb)
        out.extend(new)
    if changed:
        open(path, "w").write("\n".join(out))
    return changed


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    lo = lo_reloc_addrs(sys.argv[1])
    total = 0
    files = 0
    for f in sorted(glob.glob(os.path.join(sys.argv[2], "generated_*.c"))):
        c = fix_file(f, lo)
        if c:
            files += 1
            total += c
    print(f"rewrote {total} expression(s) across {files} file(s)")


if __name__ == "__main__":
    main()
