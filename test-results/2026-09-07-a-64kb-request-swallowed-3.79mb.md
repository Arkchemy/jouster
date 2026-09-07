# HEAPSPAN: a 64 KB request swallowed the 3.79 MB tail

Run of 2026-09-07, build `Sep  7 2026 14:09:40`, with rows keyed per pool.

```
HEAPSPAN n=2
 [ctrl=0x4400270 end=0x4500270 walks=8768
   first(@2158 0x4500268/1) lastGood(@1601290 0x4500268/8058 ...) firstShort(@0 0x0/0)]
 [ctrl=0x45002e0 end=0x4a002e0 walks=785
   first(@37019   0x4a002d8/1)
   lastGood(@204265 0x4a002d8/204 ptr=0x0 size=65536 lr=0x21db7c0)
   firstShort(@204632 0x4663a04/204)]
```

The other heap pool never goes short at all, which is a useful control.

## The arena was whole, and the moment it stopped being whole is exact

`first(@37019 0x4a002d8/1)` -- at call 37,019 the pool held **one block**
reaching `0x4a002d8`, the sentinel. That is a freshly created 5 MB heap, and
it settles the last open question from yesterday: the pool really was whole.

It stayed whole through call 204,265, still reaching the sentinel with 204
blocks. By the next walk on that pool, call 204,632, it ended at `0x4663a04`.

## What happened in between, from the block counts alone

The count is **204 before and 204 after**. No block was added or removed --
the last one simply stopped covering the tail:

```
before: free block at 0x4653a00 size 0x3ac8d4 -> ends 0x4a002d8  (the sentinel)
after : used block at 0x4653a00 size 0x010000 -> ends 0x4663a04
```

A **65,536-byte** request took the **3,852,500-byte** free block, shrank it to
`0x10000`, and the remainder header at `0x4663a04` was never written. The
orphan is `0x4a002d8 - 0x4663a04 = 3,786,964` bytes -- **exactly** the zero
region HEAPTAIL measured independently, to the byte. Two separate
measurements, same number.

The caller is `Core::jqStart+0x19c`, the job queue starting up.

Worth noting the other 64 KB blocks in HEAPTRAIL carry size `0x10004`, while
this one is `0x10000`. Same nominal size, different block size, so this
allocation did not take the same path through the allocator.

## Where the reasoning stopped being reliable

`tlsf_memalign`'s ordinary split at `0x21f0970` writes the remainder with
`stwx r6, r3, r31` -- `(block+8)+size`, which for this allocation is
`0x4663a08`, the exact word that reads zero. conquertron translates it
correctly:

```c
/* 21f0970: stwx r6, r3, r31 */
ppc_store_u32(ctx, ctx->r[3] + ctx->r[31], ctx->r[6]);
```

and the store watch lives inside `ppc_store_u32`, yet reported `hits=0` on
that address. Meanwhile the store four instructions later, which sets the
block's own size, plainly did run -- the block is `0x10000`.

So the ordinary split is not where this goes wrong, and `tlsf_memalign` has a
second, alignment-driven split earlier in the function. Rather than keep
reading disassembly -- two inferences have already had to be withdrawn this
session -- the next build checks the outcome instead of the argument.

## Next

Build `Sep  7 2026 16:14:13`, md5 `eb608afd882de6fc8ef1637fa9381dd5`, is on
the Switch. SPLITVERIFY hooks `tlsf_memalign`'s entry to stash its arguments
and its single exit at `0x21f0ac0` to check the result: follow the returned
block to its physical successor and read that successor's size word. A live
chain always has something there, a real block or the sentinel's 2 or 3, so a
**zero means the successor was never written** and everything past it is
orphaned.

It records on that predicate rather than on arrival order, so the ring holds
the allocations that actually did damage rather than the first six to run --
the mistake that wasted three earlier probes.

That names the exact call, its size, its alignment, and which block it came
from, and from there the faulty path in `tlsf_memalign` is a short read rather
than a guess.
