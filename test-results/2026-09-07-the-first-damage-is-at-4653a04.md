# SPANWATCH, bounded: the first damage is the free tail block's own header

Run of 2026-09-07, build `Sep  7 2026 17:13:13`, arena bound restored.

```
SPANWATCH n=2
 [ctrl=0x4400270 end=0x4500270 walks=17297 runaway=0
   last=0x4500268 lastWhole@1601297 drop(@0 0x0 blocks=0 by -)]
 [ctrl=0x45002e0 end=0x4a002e0 walks=1570  runaway=38
   last=0x45f37fc lastWhole@204265 drop(@204404 0x4653a00 blocks=203 by memalign)]
```

## The drop was not an artifact

With the high-water mark replaced by the real arena end, the drop reproduces:
`0x4653a00`, 203 blocks, first seen at a `tlsf_memalign` exit. The previous
run put it at call 204,398 and this one at 204,404, which is ordinary jitter.
So the earlier caution was right to withhold judgement but the finding stands.

The second pool remains a clean control: 17,297 walks, **zero runaways**, and
never once short.

## A corrected timeline

```
@204265   whole, reaches the sentinel at 0x4a002d8
@204404   ends 0x4653a00, 203 blocks     <- the free tail header reads zero
later     block at 0x4653a00 size 0x10000, ends 0x4663a04, 204 blocks
end       ends 0x45f37fc
```

Two things this shows that no previous probe could.

**The damage is progressive.** HEAPWALK reported the chain ending at
`0x4663a04` because it runs once, at the first refusal. By the end of the run
the chain ends at `0x45f37fc`, which sits between the list buffer `0x45f37dc`
and the frame manager `0x45f3964` -- that later shortening is the list overrun
already known about. There are at least two separate injuries here and they
have been conflated until now.

**The first injury is at `0x4653a00`, not `0x4663a04`.** The block that ends
up with size `0x10000` is the *second* event. The first is the free tail
block's own header going to zero while it still covered everything up to the
sentinel.

That reframes what `SPLITVERIFY n=0` meant. It checks the successor of the
block a call *returns*; the block being damaged here is a free block that no
call returned, so it was never in scope. The probe was not wrong, it was
pointed at the wrong object.

`runaway=38` is also now trustworthy: 38 walks left the arena entirely, so the
chain really does transiently hold a size word large enough to jump the
sentinel. That is a third symptom, unexplained.

## Next, and it needs no rebuild

Every watch so far has been on `0x4663a08` -- the consequence. The word that
matters is **`0x4653a04`**, the size field of the free tail block, and it has
never been watched.

`watch.cfg` now sets `store1=0x4653a04` for instruction stores and
`fill=0x4653a04` for memset and memcpy, which between them cover every route
by which that word can be written, plus `dump1=0x4653a00` for the raw header.
The NRO on the Switch is unchanged.

If the write is caught, `storewatch_writer_pc` and `storewatch_lr` name it
outright. If both watches again report nothing, then that header was never
written either, and the question becomes how a block TLSF had linked into its
free list came to have a zero size word without anybody writing it.
