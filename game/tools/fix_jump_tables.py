#!/usr/bin/env python3
"""Translate PowerPC computed jump tables into real computed gotos.

conquertron emits `bctr` as an indirect CALL plus a return:

    ppc_dispatch(ctx, ctx->ctr);
    return;

That is right for calling a function pointer and wrong for a switch, where the
target is an address inside the SAME function. It is wrong twice over here,
because the table base is folded to a synthetic .text address while
ppc_dispatch's table is keyed by real guest addresses -- so the lookup matches
nothing, ppc_dispatch has no default case and returns silently, and the
generated `return;` abandons the function. A switch compiled to a jump table
therefore does nothing at all, with no stub, no log and no crash.

The signature is unmistakable in the emitted C:

    ctx->r[8] = ctx->r[7] + <BASE>u;   /* &.text+... */   <- folded table base
    ctx->ctr  = ctx->r[8];
    ppc_dispatch(ctx, ctx->ctr);
    return;
    /* addr: b target */                                   <- the table itself
    goto L_target;
    /* addr: b target */
    goto L_target;
    ...

ctr holds BASE + index*4, so the index is (ctr - BASE)/4 and entry i is the
i-th goto. Rewrite the dispatch as a switch over those.

Requires at least 3 consecutive entries so an ordinary indirect call is never
mistaken for a table.
"""
import re, sys, glob, os

DISPATCH = "  ppc_dispatch(ctx, ctx->ctr);"
BASE_RE  = re.compile(r"^\s*ctx->r\[\d+\] = ctx->r\[\d+\] \+ (\d+)u;")
ENTRY_RE = re.compile(r"^\s*goto (L_[0-9a-f]+);\s*$")
COMMENT_RE = re.compile(r"^\s*/\* [0-9a-f]+: b 0x[0-9a-f]+ \*/\s*$")

def fix(path, apply):
    lines = open(path, encoding="utf-8").read().split("\n")
    out, i, n = [], 0, 0
    while i < len(lines):
        if lines[i] == DISPATCH and i + 1 < len(lines) and lines[i+1].strip() == "return;":
            # collect the table that follows: comment/goto pairs
            entries, j = [], i + 2
            while j + 1 < len(lines) and COMMENT_RE.match(lines[j]):
                m = ENTRY_RE.match(lines[j+1])
                if not m:
                    break
                entries.append(m.group(1))
                j += 2
            # find the folded base above
            base = None
            for k in range(i - 1, max(i - 8, -1), -1):
                m = BASE_RE.match(lines[k])
                if m:
                    base = m.group(1)
                    break
            if len(entries) >= 3 and base is not None:
                n += 1
                out.append("  /* ARKCHEMY: computed jump table. bctr branches INTO the")
                out.append("     table below, inside this same function, so this is a")
                out.append("     computed goto and not an indirect call. ctr holds the")
                out.append("     folded synthetic base plus index*4. */")
                out.append("  switch ((ctx->ctr - %su) >> 2) {" % base)
                for idx, lbl in enumerate(entries):
                    out.append("    case %du: goto %s;" % (idx, lbl))
                # A miss should be impossible: the hardware bounds-checks
                # immediately above every table. If one happens, the base
                # constant was taken from the wrong register and the result
                # would look exactly like the original bug -- a silent return.
                # Log it rather than repeat that mistake in a new costume.
                out.append("    default: arkchemy_jt_miss(ctx->ctr, %su); break;" % base)
                out.append("  }")
                out.append("  return;")
                i += 2
                continue
        out.append(lines[i])
        i += 1
    if apply and n:
        open(path, "w").write("\n".join(out))
    return n

def main():
    apply = "--apply" in sys.argv
    args = [a for a in sys.argv[1:] if a != "--apply"]
    root = args[0] if args else "."
    total = 0
    for p in sorted(glob.glob(os.path.join(root, "generated_*.c"))):
        total += fix(p, apply)
    print(("rewrote " if apply else "would rewrite ") + str(total) + " jump tables")

main()
