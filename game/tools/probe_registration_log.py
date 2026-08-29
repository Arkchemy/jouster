"""Log every class registration, in order, with its name and type index.

This is how boot progress is measured, so it must survive regeneration. It
previously existed only as an inline edit to a generated file and was lost the
first time the tree was regenerated -- along with the hard-coded window
(45 < n < 62) that was mistaken for the boot stalling at class 61 when
appendToArkCore's own hit count had been 120 all along.

Core::igMetaObject::appendToArkCore(this) is called once per class as it
registers. On the metaobject: +0x08 is the name pointer, +0x0c the type index.

Logs the first 200 registrations and then every 25th, so the frontier stays
visible without flooding the log.

Idempotent: re-running is a no-op once the marker is present.
"""
import pathlib, sys

MARKER = "ARKCHEMY-PROBE-REGISTRATION-LOG"
SIG = "void ppc_appendToArkCore__Q2_4Core12igMetaObjectFv(PpcContext *ctx) {\n"

BODY = """  /* """ + MARKER + """ -- see tools/probe_registration_log.py */
  {
      /* Declared here rather than relying on a header: the generated chunk
       * files include only the runtime headers, and which chunk this function
       * lands in changes every regeneration. */
      extern void arkchemy_bink_log(const char *fmt, ...);
      static unsigned int n_reg = 0;
      n_reg++;
      if (n_reg <= 200u || (n_reg % 25u) == 0u) {
          uint32_t np = ppc_load_u32(ctx, ctx->r[3] + 0x8u);
          char cname[72];
          uint32_t ci = 0;
          if (np) {
              for (; ci < sizeof(cname) - 1; ci++) {
                  uint8_t c = ppc_load_u8(ctx, np + ci);
                  if (!c || c < 32 || c > 126) break;
                  cname[ci] = (char)c;
              }
          }
          cname[ci] = 0;
          arkchemy_bink_log("[register] #%u  meta=0x%x  typeIndex=%u  name=\\"%s\\"",
                            (unsigned)n_reg, (unsigned)ctx->r[3],
                            (unsigned)ppc_load_u32(ctx, ctx->r[3] + 0xcu), cname);
      }
  }
"""


def main():
    base = pathlib.Path(__file__).resolve().parent.parent / "source"
    for p in sorted(base.glob("generated_*.c")):
        t = p.read_text()
        if SIG not in t:
            continue
        if MARKER in t:
            print("probe_registration_log: already applied, nothing to do")
            return
        p.write_text(t.replace(SIG, SIG + BODY, 1))
        print("probe_registration_log: instrumented appendToArkCore in %s" % p.name)
        return
    sys.exit("probe_registration_log: appendToArkCore not found -- regenerate first")


if __name__ == "__main__":
    main()
