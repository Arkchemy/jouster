# Post-increment / sequence-point audit

Prompted by the concern that `x++` does not translate as plainly as `x`, and
that index handling could be off by one. Checked rather than assumed.

## Result: no defects found

**1. Double-evaluating macros.** Every function-like macro in
`conquertron/include` was checked for a parameter appearing more than once in
its body — the classic way `FOO(i++)` increments twice. **Zero** such macros.

**2. Sequence-point violations in emitted and hand-written C.** 68 files scanned
for an object modified twice in one expression, modified and read in one
expression, and an index incremented and reused between the same pair of
sequence points. **Zero** genuine hits. The single match,
`while (*p == '/') p++;` in `cafeos_coreinit_fs.h`, is a regex false positive:
the increment is a separate statement in the loop body with a sequence point
between it and the condition. Defined behaviour.

**3. The one that actually matters here — PowerPC load/store *with update*.**
This is the real "`x++` is not `x`" hazard for a recompiler. In `lwzu`/`stwu`
the base register is modified by the same instruction that uses it, and the
architecture is specific about the order: the access uses the effective address,
*then* rA is updated. Getting that backwards would shift every access by the
displacement — precisely an off-by-one-element bug.

conquertron emits it correctly, as two statements with a sequence point between:

    /* 21ce108: lwzu r3, 4(r31) */
    ctx->r[3]  = ppc_load_u32(ctx, ctx->r[31] + (int32_t)4);
    ctx->r[31] = ctx->r[31] + (int32_t)4;

    /* 21cd654: stwu r1, -0x10(r1) */
    ppc_store_u32(ctx, ctx->r[1] + (int32_t)-16, ctx->r[1]);
    ctx->r[1] = ctx->r[1] + (int32_t)-16;

The `stwu r1, -0x10(r1)` case is the sharp one, because rS and rA are the same
register: the value stored must be the **old** r1. It is — the store reads
`ctx->r[1]` before the assignment on the next line. Prologue frame chains would
be corrupted from the first function call if this were wrong, so it is also
well covered by the fact that anything runs at all.

`lwzu rD, d(rA)` with rD == rA is architecturally invalid on PowerPC, so there
is no ambiguous case left to handle.

## Conclusion

Not a live bug class in this codebase. The boot stall is the pool-frame-manager
snapshot documented in `2026-09-06-pool-manager-snapshot.md`, not arithmetic.
