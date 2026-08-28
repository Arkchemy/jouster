#!/usr/bin/env python3
"""Bridge fix: add the missing CR0 update to record-form instructions in
already-generated C.

conquertron's codegen.cpp was missing the `if (ppc.update_cr0)` block for ten
instruction cases, so every dotted form of them left CR0 holding a *stale*
flag from an earlier instruction. That is what turned igStringBuf's `extsb.`
string-terminator check into an infinite loop during boot (2026-08-28).

codegen.cpp is now fixed, so a regenerated tree needs none of this. This
script exists only because `recomp` cannot be rebuilt on this machine right
now (no cmake, no capstone headers), and re-running it on a freshly
regenerated tree is harmless: it inserts nothing where a CR0 update already
exists.

Usage: fix_record_forms.py [--apply] <dir>
"""
import re, sys, glob, os

MNEMONICS = {"clrlwi", "extsb", "extsh", "srawi", "subfe",
             "addze", "rlwimi", "rlwnm", "adde", "addme"}
COMMENT = re.compile(r"/\* [0-9a-f]+: ([a-z0-9_.]+) ")
ASSIGN  = re.compile(r"^(\s*)ctx->r\[(\d+)\] = ")

def fix(path, apply):
    lines = open(path).read().split("\n")
    idx = [i for i, l in enumerate(lines) if COMMENT.search(l)]
    inserts = []
    for n, i in enumerate(idx):
        mn = COMMENT.search(lines[i]).group(1)
        if not mn.endswith(".") or mn[:-1] not in MNEMONICS:
            continue
        end = idx[n + 1] if n + 1 < len(idx) else len(lines)
        body = lines[i + 1:end]
        if any("ppc_cmpw" in b for b in body):
            continue
        # Insert directly after the assignment -- a label belonging to the
        # NEXT instruction can sit at the end of this range, and the CR0
        # update must land before it, not after.
        for k, b in enumerate(body):
            m = ASSIGN.match(b)
            if m and b.rstrip().endswith(";"):
                inserts.append((i + 1 + k + 1,
                                "%sppc_cmpw(ctx, (int32_t)ctx->r[%s], 0);" % (m.group(1), m.group(2))))
                break
    if apply and inserts:
        for at, text in reversed(inserts):
            lines.insert(at, text)
        open(path, "w").write("\n".join(lines))
    return len(inserts)

def main():
    args = [a for a in sys.argv[1:] if a != "--apply"]
    apply = "--apply" in sys.argv
    root = args[0] if args else "."
    total = 0
    for p in sorted(glob.glob(os.path.join(root, "generated_*.c"))):
        total += fix(p, apply)
    print(("inserted " if apply else "would insert ") + str(total) + " CR0 updates")

main()
