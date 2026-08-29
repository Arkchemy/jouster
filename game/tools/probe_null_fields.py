"""Probe: which class has null entries in its metaobject field array?

The 23:12 run answered what the null virtual calls are:

    dispatchmiss: n=72813 addr=0x0 pc=0x214d258 lr=0x2160d64 this=0x0 vt=0x0

this=0 and vt=0, so these are not vtables with holes -- they are virtual calls
on a NULL object. And frontier mask=0x00: neither igDirectory (#126) nor
igIGBFile (#127) is entered at any level, so registration has run out of
things to ask for rather than failing at something.

Those are the same problem. The site is a loop in
Core::igMetaObject::createInstanceInPlace:

    2160d3c: add   r28, r12, r6     ; start of the field array
    2160d40: add   r30, r12, r10    ; end
    2160d4c: lwz   r3,  0(r28)      ; array[i]
    2160d50: lwz   r12, 0(r3)       ; its vtable        <- r3 is NULL here
    2160d54: lwz   r6,  0x124(r12)
    2160d60: bctrl                  ; virtual call on the field
    2160d64: addi  r28, r28, 4

It walks a metaobject's field array calling one virtual method on each entry.
A null entry means the field was never instantiated, so the object is built
without its fields, and nothing downstream ever asks for the next class.

createInstanceInPlace's own `this` is the metaobject, and a metaobject's name
pointer sits at +0x8 -- the same offset the registration log reads. So the
class can be named outright rather than inferred.

Records, on the first null entry seen: the metaobject, its name, how many
entries the array holds and how many of them are null.

    nullfield: hits=%u meta=0x%x n=%u nulls=%u name="%s"

  nulls == n      -> the whole array is empty; field allocation never ran
  nulls < n       -> some fields exist and some do not, which points at the
                     per-field construction rather than the allocation
  name            -> the class to look at next

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

MARKER = "ARKCHEMY-PROBE-NULLFIELD"
OLD = ("  /* 2160d4c: lwz r3, 0(r28) */\n"
       "  ctx->r[3] = ppc_load_u32(ctx, ctx->r[28] + (int32_t)0);\n")
NEW = ("  /* 2160d4c: lwz r3, 0(r28) */\n"
       "  ctx->r[3] = ppc_load_u32(ctx, ctx->r[28] + (int32_t)0);\n"
       "  /* " + MARKER + " -- see tools/probe_null_fields.py */\n"
       "  if (ctx->r[3] == 0u) {\n"
       "    extern unsigned int g_arkchemy_nf_hits, g_arkchemy_nf_meta, g_arkchemy_nf_n, g_arkchemy_nf_nulls;\n"
       "    extern char g_arkchemy_nf_name[64];\n"
       "    if (g_arkchemy_nf_hits == 0u) {\n"
       "      unsigned int __i, __n = (ctx->r[30] - ctx->r[28]) / 4u + 1u, __nulls = 0u, __np;\n"
       "      if (__n > 4096u) __n = 4096u;\n"
       "      for (__i = 0; __i < __n; __i++)\n"
       "        if (ppc_load_u32(ctx, ctx->r[28] + __i * 4u) == 0u) __nulls++;\n"
       "      g_arkchemy_nf_meta = ctx->r[29];\n"
       "      g_arkchemy_nf_n = __n;\n"
       "      g_arkchemy_nf_nulls = __nulls;\n"
       "      __np = ppc_load_u32(ctx, ctx->r[29] + 0x8u);\n"
       "      if (__np) {\n"
       "        for (__i = 0; __i < 63u; __i++) {\n"
       "          unsigned char __c = (unsigned char)ppc_load_u8(ctx, __np + __i);\n"
       "          if (__c < 32u || __c > 126u) break;\n"
       "          g_arkchemy_nf_name[__i] = (char)__c;\n"
       "        }\n"
       "      }\n"
       "    }\n"
       "    g_arkchemy_nf_hits++;\n"
       "  }\n")


def main():
    base = pathlib.Path(__file__).resolve().parent.parent / "source"
    for p in sorted(base.glob("generated_*.c")):
        t = p.read_text()
        if OLD not in t:
            continue
        if MARKER in t:
            print("probe_null_fields: already applied, nothing to do")
            return
        SIG = "void ppc_createInstanceInPlace__Q2_4Core12igMetaObjectCFPvPQ2_4Core12igMemoryPool(PpcContext *ctx) {"
        if SIG not in t:
            continue
        p.write_text(t.replace(OLD, NEW, 1))
        print("probe_null_fields: instrumented the field walk in %s" % p.name)
        return
    sys.exit("probe_null_fields: field walk not found -- regenerate first")


if __name__ == "__main__":
    main()
