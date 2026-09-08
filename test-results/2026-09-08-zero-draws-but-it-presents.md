# GX2DRAW: zero draws, 1,291 presents — and the reason is upstream

Run of 2026-09-08, build `Sep  8 2026 07:56:46`.

```
GX2DrawEx              x0        GX2Flush            x1291
GX2DrawIndexedEx       x0        GX2SwapScanBuffers  x1291
GX2DrawDone            x0        GX2GetSwapStatus    x1291
GX2BeginDisplayListEx  x0        GX2SetSwapInterval  x1
GX2EndDisplayList      x0
```

The engine **presents 1,291 frames** and never submits a single triangle. The
per-frame state and present paths are complete; the geometry submission path is
never entered at all.

Note the asymmetry that makes this precise: `GX2CopyDisplayList` runs 13,139
times while `GX2BeginDisplayListEx` and `GX2EndDisplayList` run **zero** times.
It copies display lists it never builds. Draws would be recorded between Begin
and End, so an untouched pair is the same fact from another angle.

## This is not a graphics problem

The obvious reading -- that the renderer is broken -- is wrong. There is
nothing to draw:

```
fs: open=5  rd=0/0  ard=1/131072      the file is 198,695 bytes
asyncq: q=1 done=1 drop=0 pend=0      nothing outstanding
archq: n=0                            work list empty
wi1/wi2: ty=255 st=255                both work items freed
INFLATE lzma n=1                      one block, where 35 were expected
```

**Exactly 131,072 bytes of a 198,695-byte file were read, in a single request,
and nothing further was ever asked for.** 128 KB is a round buffer size, not a
file size, so this is a chunked load that never requested chunk two. The engine
is not blocked on I/O -- `pend=0` -- it simply stopped.

So the scene is empty because the archive never finished loading, and the
renderer is faithfully drawing an empty scene. Shader translation, the wall
this project has been bracing for, is **not** the current blocker.

## Next

Build `Sep  8 2026 08:10:39`, md5 `4384e814256d360ac25466f00153a42d`, is on the
Switch.

**ARCHPUMP** counts the archive's read loop end to end:
`igArchive::updateArchiveSystem` → `updateTasks` → `startNewTasks` →
`startBlockRead`, plus `decompressBatch`, `addWork`, and
`igArchiveBlockManager::allocate` with its non-null returns counted separately
from its calls, and `getNumAvailableBlocks` with its last returned value.

That last pair is there because an exhausted block pool is the obvious way for
`startNewTasks` to run happily every frame and still start nothing -- a
distinction the call counts alone would hide.

Reading the result:

* **the chain stops partway** -- e.g. `updateTasks` runs but `startNewTasks`
  does not -- and the gate is in that link
* **the whole chain runs but `startBlockRead` is zero** -- something declines
  every candidate, and `allocate`/`gotBlock` says whether it is the block pool
* **`gotBlock` well below `alloc`** -- the block manager is out of blocks, and
  the question becomes why they are never recycled

Whichever it is, this is a load-scheduling problem in the archive system rather
than anything to do with rendering.
