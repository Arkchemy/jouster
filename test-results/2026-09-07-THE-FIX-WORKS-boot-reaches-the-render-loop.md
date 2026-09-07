# The fix works: the archive drains and the engine reaches its render loop

Run of 2026-09-07, build `Sep  7 2026 23:17:20` -- the linkage-area fix.

## The corrupting write is gone

```
WRITELOG n=7  -- all seven from thr=0xdd9fdfe0, all lr=0x21f07a4 (tlsf_memalign)
```

Every write to `0x4653a04` is now the allocator's own, from a single thread.
The stray zero from a second thread that never entered the allocator is gone.

## The arena stays whole

```
SPANWATCH n=3
 [ctrl=0x4400270 walks=17980 runaway=0 last=0x4500268 drop(@0 ... by -)]
 [ctrl=0x45002e0 walks=2748  runaway=0 last=0x4a002d8 drop(@0 ... by -)]
 [ctrl=0x4c00eac walks=22    runaway=0 last=0x4d90ea4 drop(@0 ... by -)]
SPLITVERIFY calls=9658 n=0
HEAPWALK  <never ran -- it only fires on the first refusal, and there were none>
```

`0x45002e0` reaches its sentinel at `0x4a002d8` with **no drop**, across 2,748
walks and zero runaways. A **third** heap pool now exists and is also whole,
which only happens because boot got far enough to create it.

## Before and after

```
                        before            after
  guest calls           4,487,759         8,842,835      +97%
  files opened          2                 5
  LZMA pool 28          resolved=0x0      resolved=0x4427ebc
  LZMA probability arr  ptr=0x0           ret=0xe1b7570
  heap chain            3.79 MB orphaned  reaches the sentinel
  SPLITVERIFY           n=1               n=0
  heap pools            2                 3, all whole
  archive work list     n=1               n=0   <- it drained
```

LZMA now decodes correctly:

```
LZMA err=0x0 (0=ok) | out got=0x118a expected=0x118a | in got=0x8000 expected=0x8000
```

Output and input lengths both match expectation exactly.

## Where the engine is now

`last_pc` resolves to `Insight::igInsightCore::getSystemFromMeta`, and the pc
samples -- 2,156 distinct points, up from 1,095 -- land in:

```
Gfx::igPlatformVisualContext::setSurfaces
Gfx::igBaseVisualContext::setMatrix -> ASM_MTX44Concat
Math::igMatrix44f::invert  <- from Sg::igCommonTraversal::setViewMatrix
Sg::igCommonTraversalInstance::end
Insight::igInsightSystem::acquireResource
```

Scene-graph traversal, view matrices, visual-context surfaces. **The engine is
running a render loop.** It completed all 25,200 frames and exited normally,
with no exception, abort or assert anywhere in the log.

## One thing deliberately not changed

`INFLATE ... ok=0 fail=1` still counts this run as a failure, because the probe
tests `ret == 1` and the call returned 0. But `LZMA err=0x0` with both lengths
matching says the decompression itself was correct, and the archive drained.

The `ret == 1` test may still be right -- the note beside it records that
`decompressBatch` requires exactly 1 -- so flipping the classifier blind could
hide a real signal. Left alone, and flagged here instead.

## Next

Build `Sep  7 2026 23:37:58`, md5 `fb60b88640fba3dbe69e4c0691464f9e`, is on the
Switch.

**GX2CENSUS** counts every GX2 import the engine calls, keyed by call-site `lr`
so names resolve offline against the retail symbol table -- 140 shims hooked.
The render loop is running engine-side; the question is whether it reaches the
graphics hardware at all.

* `total=0` means the loop runs entirely in the scene graph and never issues a
  draw, and the next work is finding what gates that
* `total>0` names exactly which GX2 entry points the game uses first, which is
  the list that has to work before anything appears on screen

Either answer is the starting point for the graphics work, which was always
going to be the largest wall on this project.
