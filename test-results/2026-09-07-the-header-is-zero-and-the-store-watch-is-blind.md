# HEAPSTOP: the header is zeroed, and the store watch could never have seen it

Run of 2026-09-07, build `Sep  7 2026 13:09:21`.

```
HEAPSTOP at=0x4663a04 words=[0x0,0x0,0x0,0x0] prev=0x4653a00 prevSize=0x10000
HEAPWALK blocks=211 used=211/1451880b free=0/0b largest=0
         range=0x4500f50..0x4a002e0 stoppedAt=0x4663a04 status=1
```

## The walk stopped on zeros, not on a sentinel

`tlsf_create` writes its end sentinel as size 0 with the used and prev_free
bits, so the size word reads **2 or 3**. All four header words at `0x4663a04`
read **0**. Whatever stopped the walk, it was not the heap legitimately
ending.

The previous block is consistent: `0x4653a00` with size word `0x10000`, and
`0x4653a00 + 4 + 0x10000 = 0x4663a04` exactly. So the chain is intact right up
to the point where it runs into zeroed memory.

## And tlsf_create was not shortchanged

Reading its head settles the other branch. Given `(mem, size)` it computes

```
21f051c: addi   r6, r4, -0xc7c        ; poolBytes = size - 0xc7c
21f0520: rlwinm r31, r6, 0, 0, 0x1d   ; & ~3
21f0524: cmplwi r31, 0xc              ; reject below 12
21f0530: lis    r0, 0x4000            ; reject above 1 GB
```

For `_size` of `0x500000` that is `0x4FF384` -- no cap, no clamp. `activate`
passes `_address` and `_size` straight in, so the arena really was built as
5 MB. The heap should span the whole thing.

## The correction: hits=0 never meant what I said it meant

The store watch on `0x4663a08` reported `hits=0`, and I was about to read that
as "nothing ever wrote here". It does not mean that.

`ppc_import_coreinit_memset` and `_memcpy` operate on `ctx->shared->mem`
directly with host `memset`/`memmove` -- deliberately, for speed on
texture-sized copies -- so they never pass through `ppc_store_u32` and the
store watch cannot observe them at all:

```c
static inline void ppc_import_coreinit_memset(PpcContext *ctx) {
    uint32_t dst = ctx->r[3] & (uint32_t)(PPC_MEM_SIZE - 1);
    ...
    memset(&ctx->shared->mem[dst], c, n);
}
```

So `hits=0` means "no translated store instruction touched this word", and a
bulk clear over the range produces exactly the evidence seen: sixteen zero
bytes with no recorded store. A memset is now the leading explanation rather
than an excluded one, and it was never excluded in the first place.

Worth noting `reallocCommon` itself memsets freed and fresh blocks with 0xfd
and 0xcd, so the engine does bulk-write into this arena as a matter of course.
Those particular fills are not zero, but they establish that the path is
live.

## Next

Build `Sep  7 2026 13:37:06`, md5 `16edad8255837f523345abedceaee841`, is on
the Switch, with three additions that make the run informative whichever way
it goes:

* **BULKWRITE** -- `ark_note_bulk` in both shims records any memset or memcpy
  whose destination range covers the new `fill=` address in `watch.cfg`, with
  the lr that issued it. This closes the blind spot above.
* **HEAPTAIL** -- scans from the stop point to the arena end and reports how
  many non-zero words are out there and where. If the arena really is 5 MB,
  `tlsf_create`'s sentinel is somewhere in that region and this finds it. If
  the whole 3.79 MB is zero, the memory was cleared or never built in.
* **HEAPTRAIL** -- the last 8 blocks walked with their raw size words, so a
  chain that derailed on one bad size shows the bad size rather than only its
  consequence. `prevSize=0x10000` is a suspiciously round 64 KB and deserves
  to be seen in context.

`watch.cfg` gains `fill=0x4663a08` alongside the existing store watch, so both
kinds of writer are covered this time.
