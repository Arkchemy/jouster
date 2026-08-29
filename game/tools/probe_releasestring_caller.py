#!/usr/bin/env python3
"""Probe: capture who calls igStringPool::releaseString with a bogus pool.

The boot hangs forever in igStringPool::remove walking a bucket chain that has
no NULL check, at bucket index 0x811C9DC5. That index is the raw FNV-1a basis,
which means two things at once (see searchForString at 0x21a54e4):

    21a54f0: cmpwi r10, 0 / beq 0x21a5518   -> string is EMPTY, hash never mixed
    21a5518: lwz  r9, 0x1c(r3)              -> bucketCount
    21a551c: divwu r10, r0, r9              -> hash % bucketCount, bucketCount = 0

Hardware registers at the hang say why bucketCount is 0:

    r3 (pool) = 0x4400204
    r4 (item) = 0x44001F8      and 0x44001F8 + 0xc == 0x4400204

igStringPoolItem stores its characters inline at +0xc, so the "pool" pointer
IS the item's own string buffer. It was never a pool, so +0x1c is not a bucket
count and +0xc is not a bucket array.

releaseString(this=pool, item, container) receives that bogus pool as its
argument, so the mistake belongs to ITS caller -- and there are ten call sites
in generated_0161.c alone. g_ppc_last_caller_lr is overwritten by the calls
releaseString makes before the hang, so the log cannot currently say which one.

The watch slots cannot answer this either: all 8 are in use, the struct has no
lr field, and adding one means regenerating 8.7M lines. So capture it here
instead, in the one function that matters.

Records the first call whose pool == item + 0xc, keeping its lr, plus running
totals. That lr is the return address inside the guilty caller, which pins the
call site exactly.

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

SRC = pathlib.Path(__file__).resolve().parent.parent / "source" / "generated_0161.c"
MARKER = "ARKCHEMY-PROBE-RELSTR"

OLD = (
    "void ppc_releaseString__Q2_4Core12igStringPoolFPCQ2_4Core16igStringPoolItemPQ2_4Core21igStringPoolContainer(PpcContext *ctx) {\n"
    "  g_ppc_last_caller_lr = ctx->lr;\n"
)

NEW = (
    "void ppc_releaseString__Q2_4Core12igStringPoolFPCQ2_4Core16igStringPoolItemPQ2_4Core21igStringPoolContainer(PpcContext *ctx) {\n"
    "  /* " + MARKER + " -- see tools/probe_releasestring_caller.py */\n"
    "  {\n"
    "    extern unsigned int g_arkchemy_relstr_calls, g_arkchemy_relstr_bad;\n"
    "    extern unsigned int g_arkchemy_relstr_first_bad_lr, g_arkchemy_relstr_last_lr;\n"
    "    extern unsigned int g_arkchemy_relstr_pool, g_arkchemy_relstr_item, g_arkchemy_relstr_cont;\n"
    "    g_arkchemy_relstr_calls++;\n"
    "    g_arkchemy_relstr_last_lr = ctx->lr;\n"
    "    g_arkchemy_relstr_pool = ctx->r[3];\n"
    "    g_arkchemy_relstr_item = ctx->r[4];\n"
    "    g_arkchemy_relstr_cont = ctx->r[5];\n"
    "    if (ctx->r[3] == ctx->r[4] + 0xcu) {\n"
    "      g_arkchemy_relstr_bad++;\n"
    "      if (g_arkchemy_relstr_first_bad_lr == 0u) g_arkchemy_relstr_first_bad_lr = ctx->lr;\n"
    "    }\n"
    "  }\n"
    "  g_ppc_last_caller_lr = ctx->lr;\n"
)


def main():
    if not SRC.exists():
        sys.exit("missing %s -- regenerate first" % SRC)
    text = SRC.read_text()
    if MARKER in text:
        print("probe_releasestring_caller: already applied, nothing to do")
        return
    if text.count(OLD) != 1:
        sys.exit(
            "probe_releasestring_caller: expected exactly 1 releaseString "
            "preamble, found %d -- refusing to guess" % text.count(OLD)
        )
    SRC.write_text(text.replace(OLD, NEW))
    print("probe_releasestring_caller: instrumented releaseString in %s" % SRC.name)


if __name__ == "__main__":
    main()
