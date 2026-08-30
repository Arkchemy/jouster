#!/usr/bin/env python3
"""Poll the watched memory address at every recompiled function entry.

conquertron's store watch only sees writes routed through ppc_store_u32 /
ppc_store_u8. Anything writing ctx->shared->mem directly -- a memset shim, a
memcpy -- never touches it. On 2026-08-30 that gap was the whole question:
the store watch reported exactly four writes to the memory-context global
(.data+5336, real address 0x100CD318), the first of them correct at call
3,625, and the value read back zero at call 414,746. Either one of the other
three wrote the zero, or something that never calls ppc_store_u32 did, and a
watch that reports only the latest write cannot tell those apart.

codegen.cpp now emits this poll itself, so a freshly regenerated tree already
has it and this script is a no-op there. It exists for the far more common
case of an existing generated tree carrying a dozen applied probes, where a
full regeneration to pick up one line would mean re-applying all of them.

Usage: tools/probe_memwatch_poll.py [--apply] [source-dir]
"""
import sys, pathlib

MARK = "for (int __w = 0; __w < ARKCHEMY_WATCH_SLOTS; __w++)"
POLL = "  ppc_poll_watch_mem(ctx);\n"

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    apply = "--apply" in sys.argv
    src = pathlib.Path(args[0] if args else
                       pathlib.Path(__file__).resolve().parent.parent / "source")
    files = sorted(src.glob("generated_*.c"))
    if not files:
        print(f"no generated_*.c under {src}", file=sys.stderr)
        return 1
    total = skipped = 0
    for f in files:
        lines = f.read_text().splitlines(keepends=True)
        out, added = [], 0
        for i, line in enumerate(lines):
            out.append(line)
            if MARK in line:
                if i + 1 < len(lines) and "ppc_poll_watch_mem" in lines[i + 1]:
                    skipped += 1
                    continue
                out.append(POLL)
                added += 1
        if added and apply:
            f.write_text("".join(out))
        total += added
    print(f"{'applied' if apply else 'would apply'}: {total} poll site(s)"
          f"{f', {skipped} already present' if skipped else ''}")
    if not apply:
        print("re-run with --apply to write")
    return 0

sys.exit(main())
