# HEAPTAIL: the arena was built whole, and the missing 3.79 MB is untouched

Run of 2026-09-07, build `Sep  7 2026 13:37:06`.

```
BULKWRITE seen=0 n=0 <none>
HEAPTAIL  nonzero=2 first=0x4a002d8 last=0x4a002dc scanned=946743
HEAPTRAIL [0x45f3978 sz=0x40004] [0x4633980 sz=0x18] [0x463399c sz=0x18]
          [0x46339b8 sz=0x10004] [0x46439c0 sz=0x18] [0x46439dc sz=0x18]
          [0x46439f8 sz=0x10004] [0x4653a00 sz=0x10000]
HEAPSTOP  at=0x4663a04 words=[0x0,0x0,0x0,0x0] prev=0x4653a00 prevSize=0x10000
```

## The heap was built correctly

`tlsf_create` computes `poolBytes = (0x500000 - 0xc7c) & ~3 = 0x4ff384`, which
puts its end sentinel at `0x4500f50 + 4 + 0x4ff384 = 0x4a002d8`. HEAPTAIL
scanned 946,743 words past the stop point and found exactly **two** non-zero
ones: `0x4a002d8` and `0x4a002dc`. That is the sentinel, exactly where a full
5 MB pool puts it.

So the arena was never short. This closes the "setup shortened the heap"
branch outright.

## The chain did not derail either

HEAPTRAIL's eight blocks chain perfectly, each `next = block + 4 + size`
landing on the following one, and every size is a plausible allocation --
`0x18` for small objects, `0x10004` and `0x40004` for 64 KB and 256 KB
buffers, all flagged used. `prevSize=0x10000` was worth suspecting and is
fine. The walk was reading a real chain right up to where it stops.

## And nothing wrote the missing region

Between the last block's end at `0x4663a04` and the sentinel at `0x4a002d8`
lie **3,786,964 bytes -- 946,741 words -- and not one is non-zero**.

`BULKWRITE seen=0` closes the blind spot found last time: no memset or memcpy
covered that address either. Between the two watches, nothing wrote it by any
route, and guest memory starts zeroed.

So the remainder header at `0x4663a04` was **never written**. That region has
been untouched since boot. TLSF simply stopped splitting off remainders at
some point, and everything after that had nothing to allocate from.

## What is left to measure

Three explanations are now dead: the arena was not built short, the chain did
not derail on a bad size, and nothing scribbled on the free block. What
remains is a question of *when* -- at some allocation, the free block TLSF was
working with stopped covering the tail, and every allocation after that was
served from an ever-shrinking remnant until there was none.

That is directly measurable. A healthy walk ends on the sentinel, 8 bytes
below the arena end. Walking on every call and recording the first walk that
falls short names the exact allocation that lost the tail.

Build `Sep  7 2026 13:54:30`, md5 `be32f7c2b30539c7746b495d0c7bc4b4`, is on
the Switch. HEAPSPAN hooks `reallocCommon`'s entry, before the call does
anything, walks the chain when the pool is an `igHeapMemoryPool`, and reports
the last span that still reached the sentinel alongside the first that did
not, with its call number. `OWNER` can then say what that allocation was.

The walk is bounded by the same block cap and range check as HEAPWALK and
returns rather than recording anything if either trips, so a chain in a bad
state cannot spin or poison the result.
