# SPLITVERIFY clears tlsf_memalign

Run of 2026-09-07, build `Sep  7 2026 16:14:13`.

```
SPLITVERIFY calls=8650 n=0 <none>
HEAPSPAN    [ctrl=0x45002e0 ... lastGood(@204265 ...) firstShort(@204633 0x4663a04/204)]
HEAPWALK    blocks=211 used=211/1451880b free=0/0b stoppedAt=0x4663a04
HEAPSTOP    at=0x4663a04 words=[0x0,0x0,0x0,0x0] prev=0x4653a00 prevSize=0x10000
```

The orphan is identical to the previous run, so the probe was live and simply
did not see the culprit: **8,650 `tlsf_memalign` calls, and not one returned a
block whose physical successor was missing.**

## Which rules out the theory it was built to test

Both fresh-allocation paths do go through it --
`igHeapMemoryPool::mallocInternal` and `mallocInternalUntracked` each
`bl 0x21f0710` -- so `jqStart`'s 64-byte-aligned 64 KB request really did pass
through this check and came back clean. The block it returned had a successor
at the moment it returned.

Put beside two earlier results, the picture is quite constrained:

* the store watch on `0x4663a08` reported `hits=0` -- no store instruction
* `BULKWRITE seen=0` -- no memset or memcpy either
* SPLITVERIFY `n=0` -- no allocation left a block without a successor

So the header at `0x4663a04` was never written by anything, and the shrink was
not performed by the allocation path.

## What was never being watched

`tlsf_free` and `tlsf_realloc` also rewrite the physical chain -- free
coalesces with its neighbours, realloc trims in place -- and neither was
instrumented. Worse, `igMemoryPool::freeUntracked` and
`igHeapMemoryPool::freeInternalUntracked` reach `tlsf_free` **without going
through `reallocCommon`**, so the HEAPSPAN walk never fires for them. A whole
class of chain-modifying calls has been invisible this entire time.

A correction to something I said last time, too: I claimed the window between
`lastGood@204265` and `firstShort@204633` contained exactly one call. That is
true only of calls to `reallocCommon` on this pool. Roughly 368 other function
calls happened in that window, and any of them could hold the culprit.

## Next

Build `Sep  7 2026 16:57:51`, md5 `446cedaf987659b708db1f6abf349e92`, is on
the Switch.

SPANWATCH needs no arena size and makes no assumption about which function is
at fault: it walks the chain to its terminator, remembers the furthest that
chain has ever reached for each control block, and records the first walk that
falls **below that high-water mark**, tagged with the entry point that saw it.
It is hooked at four places -- `reallocCommon`'s entry and the exits of
`tlsf_memalign`, `tlsf_free` and `tlsf_realloc`.

Whichever tag comes back is the function that shortened the chain, and the two
new ones are precisely the paths that have never been observed.
