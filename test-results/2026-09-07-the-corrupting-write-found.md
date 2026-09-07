# The corrupting write: a size marker landing on its own block header

Run of 2026-09-07, build `Sep  7 2026 17:13:13`, watching `0x4653a04`.

```
storewatch(0x4653a04)  hits=7@204401
  value      = 0x10000        writer_pc = 0x21f04f0     lr = 0x217cc58
  r29 = 0x4500274 (the pool)  r31 = 0x4653a08 (ptr)     r30/r4 = 0x10000
BULKWRITE seen=0
DUMP1 0x4653a00  00:00010000 04:00010000 08:045002e0 0c:045002e0
```

Seven instruction stores to the free tail block's size word. Watching
`0x4663a08` all this time was watching the consequence.

## The write

`writer_pc` names `tlsf_block_size`, but that function is six instructions and
pure read -- correctly translated, no store in it. As `ppc_store_u32`'s own
comment warns, `g_ppc_current_pc` is the last function *entered*, so this is
the caller resuming after the `bl`. `lr=0x217cc58` places it exactly, in
`igHeapMemoryPool::mallocInternal`:

```
217cc48: bl   0x21f0710        ; ptr = tlsf_memalign(...)
217cc54: bl   0x21f04f0        ; r3  = tlsf_block_size(ptr)
217cc58: add  r12, r31, r3     ; ptr + blockSize
217cc64: addi r0, r12, -4      ; ptr + blockSize - 4
217cc70: rlwinm r12, r0, 0, 0, 0x1d
217cc78: stw  r4, 0(r12)       ; *(that) = requested size
```

A trailing size marker, written into what should be the block's last word.
The address observed was `0x4653a04` with `ptr = 0x4653a08`, so
`ptr + blockSize - 4 == ptr - 4`, which means **`tlsf_block_size` returned 0**
and the marker landed on the block's own header instead.

`tlsf_block_size` reads `*(ptr-4) & ~3`. So the block `tlsf_memalign` had just
returned already carried a zero size word, and this write then stamped
`0x10000` -- the requested size -- over it. That is the `0x10000` seen ever
since, and it is not a block size at all but a size marker in the wrong place.

## Why SPLITVERIFY reported n=0

Its predicate was

```c
if (__bsz && ppc_load_u32(ctx, __succ + 4u) == 0u)
```

With `__bsz == 0` the `&&` short-circuits and nothing is recorded. A returned
block whose own size is zero -- precisely this case -- was the one case the
probe skipped. It was not evidence that `tlsf_memalign` is clean.

Fixed to `if (__bsz == 0u || load(succ+4) == 0u)`, which catches both faults
and treats a zero-size return as the more serious of the two. Build
`Sep  7 2026 20:08:36`, md5 `63f34ca8bddcba55a94136ec29dfd8b9`, on the Switch.

## What is still unexplained

Why `tlsf_memalign` returns a block whose size word is zero. The store watch
shows seven writes to that word, and only the last is characterised; the
earlier ones may include whichever first set it to zero. The fixed
SPLITVERIFY will catch the return itself, with the requested size and
alignment, which is a much narrower question than the one this session opened
with.
