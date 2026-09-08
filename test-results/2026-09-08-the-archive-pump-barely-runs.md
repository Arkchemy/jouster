# ARCHPUMP: the archive system is pumped 8 times in 25,200 frames

Run of 2026-09-08, build `Sep  8 2026 08:10:39`.

```
ARCHPUMP updateArchiveSystem=8 updateTasks=8 startNewTasks=8 startBlockRead=1
         decompressBatch=1 addWork=5
       | blockMgr alloc=2 gotBlock=0 avail(calls=8 last=6)
```

## The number that matters

`updateArchiveSystem=8`. Across a 25,200-frame run, the archive system was
pumped **eight times**. Every one of those eight reached `startNewTasks`, and
between them they started **one** block read and ran **one** decompression, out
of the 35 blocks the file needs.

`addWork=5` shows work was handed to the archive five times, so requests were
being made. They were simply never turned into reads.

## A flaw in the probe, and what it invalidates

`gotBlock=0` cannot be trusted, and the reason is my own.
`igArchiveBlockManager::allocate` has **two** exits:

```
216e2a8: lwz   r9, 0x14(r3)   ; state
216e2ac: cmpwi r9, 0
216e2b0: beqlr                ; state == 0 -> return this block IMMEDIATELY
...
216e2e4: blr                  ; otherwise return the best aged candidate
```

The hook was inserted before the final `blr` only -- the regex matched `blr`
and not `beqlr` -- so a block returned through the early exit was never
counted. `alloc=2 gotBlock=0` therefore cannot distinguish "allocation failed
twice" from "allocation succeeded twice through an exit I was not watching".

That matters because it was about to look like a contradiction worth chasing:
`getNumAvailableBlocks` reports **6** available while `allocate` appeared to
return nothing. Reading both functions shows they agree on the definition --
`getNumAvailableBlocks` counts blocks with state 0 **or** 2, and `allocate`
takes a state-0 block immediately or the oldest state-2 one -- so a genuine
disagreement would have been significant. On this evidence there may be no
disagreement at all.

## What still stands

Independent of the flawed counter:

* the pump ran only 8 times
* only 1 of 35 blocks was read and decompressed
* `fs: ard=1/131072` of a 198,695-byte file, `pend=0`, nothing outstanding
* the renderer presents 1,313 frames and draws nothing, because the scene is
  empty

## Next

Build `Sep  8 2026 19:43:50`, md5 `694063cb9addee7413e9e414deefcdda`, is on the
Switch.

**ARCHBLK** fixes the missed exit -- the early `beqlr` now increments its own
counter, reported separately from the final `blr` -- and histograms the states
the availability walk actually sees: how many blocks are state 0, state 2, and
anything else.

That resolves the ambiguity three ways:

* **allocEarly > 0** -- allocation was working all along and the block manager
  is not the problem; the question becomes why eight pumps produced one read
* **allocEarly = 0 and states show zero=0 two=6** -- the six "available" blocks
  are all aged state-2 entries and `allocate`'s age comparison is rejecting
  them, which is a real bug in a five-instruction window
* **states show something other than 0 or 2** -- `getNumAvailableBlocks` and
  `allocate` genuinely disagree, and that disagreement is the bug

The wider question -- why `updateArchiveSystem` is called eight times rather
than every frame -- is worth answering regardless of which way that lands.
