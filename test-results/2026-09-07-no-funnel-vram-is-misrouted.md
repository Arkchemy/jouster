# The funnel hypothesis is wrong; pool routing works

2026-09-07, build `Sep  7 2026 01:56:17`.

## Pool resolution is healthy

`POOLMAP` -- 11 distinct indices, 11 distinct pools:

```
idx 0  -> 0x4400204  x13888      idx 8  -> 0x4503638  x1
idx 1  -> 0x44001a8  x4          idx 9  -> 0x450369c  x18
idx 2  -> 0x4500274  x553        idx 14 -> 0x4427ebc  x40
idx 3  -> 0x4a002e4  x20         idx 16 -> 0x442964c  x37
idx 6  -> 0x4408d74  x249        idx 17 -> 0x4429938  x85
idx 7  -> 0x45035d4  x7
```

No collapsing. `getMemoryPoolByIndex` returns a different pool for every
index, and `POOLDIST` shows allocations genuinely spread across 12 pools.
**The funnel idea is dead**, and with it the theory that the 5 MB arena fills
because everything lands in it.

## A flaw in my own metric

`POOLDIST` counts an allocation as failed when `reallocCommon` returns NULL.
But `reallocCommon` is also the **free** path, and a free returns NULL
legitimately. So its "ok" figures undercount, badly -- 0x4400204 reads 97% and
0x44001a8 reads 52% mostly because one of them frees more than the other.

`ALLOCONLY` excludes frees and is the number to trust:

```
pool 0x4500274:  589 allocations, 288 ok  ->  49% genuinely fail
```

That failure rate is real. The rest of `POOLDIST`'s rates are not evidence of
anything until the metric distinguishes `size == 0`.

## What still stands

The 350 MB `igCafeSystemMemory::enableVRAM` request is routed to pool
`0x4500274`, a 5 MB general-purpose arena. On Wii U, VRAM comes from MEM1 or a
graphics reservation, not from a game pool that size. So there is a routing
problem after all -- just a targeted one, for VRAM-class allocations, rather
than the wholesale collapse I guessed at.

Whether that misrouting is also what exhausts the pool for everything else is
not established. A 5 MB arena serving 288 successful allocations could simply
be full on its own.

## Next

Track cumulative bytes in and out of `0x4500274` and compare against its
`0x500000` size. That separates the two remaining explanations outright:

* **high-water reaches ~5 MB** -- the pool is genuinely exhausted, and the
  question becomes what is holding so much, with the misrouted VRAM request a
  prime suspect
* **high-water stays well under 5 MB** -- the pool refuses while it still has
  room, which is an allocator bug rather than a capacity problem

Worth noting `[pool+0x50]` read 0 at every refusal, and it looked like a
high-water field. If it is, it never moved despite 288 successes, which favours
the second explanation.
