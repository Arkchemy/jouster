# The wait is gone

Build `Sep 18 2026 02:28:15`.

    OK  build=Sep_18_2026_02-28-15 flush=1 frames=14400 draws=12807 modules=7

                      block-read (01:04)   fence (02:28)
    game frames                  5,689           6,318
    frame_avg                     62ms            55ms
    draws                       11,544          12,807
    waitidle                  39,572ms             5ms

**39,572ms to 5ms.** The unconditional queue wait is not reduced, it is gone —
five milliseconds across a six-and-a-half-minute run means the fence for the
slot being reused was already signalled essentially every time. The GPU never
falls a rotation behind on a frame this light, which is exactly what the change
assumed and is now measured rather than hoped.

**16.1 fps to 18.2 fps.** Modest, and it should be: the wait was 11% of a run,
and removing 11% gives about 1.12x. It landed at 1.13x. The budget predicted
the outcome to within a point, which is worth more than the speedup.

## Correct, not just fast

The first check was never the speed. A wrong fence hangs or corrupts.

    modules=7
    SURFPOOL  live=4 hit=31578 miss=4 evicted=0
    DRAWSIZE  quad=12755 max=4
    DKDEBUG   none

Four surfaces, four misses, nothing evicted, every draw still a four-vertex
quad, and not one line of deko3d validation output across 6,318 frames. The
retirement rework held: `leaked=0`.

## A probe's own guidance went stale

    RETIRE deferred=69488 drained=69459 peak=11 leaked=0

`deferred - drained` is 29 against `peak=11`, and the probe's text says a
standing gap larger than peak is a leak. By its own words, that reads as a
leak. It is not.

The list is per command slot now, so up to `ARKCHEMY_GX2_CMD_SLOTS` buckets are
in flight at once — 3 x 11 = 33, and 29 sits under it. The criterion was
written when there was one bucket and nobody updated it when there were three.
Corrected in the probe text so the next reader is not sent chasing a leak that
is not there.

Worth noting as a pattern: this is the third probe this week whose own
explanation outlived its accuracy — after `GPUCNT`'s 32-bit width and the
roadmap's "fails two runs in five". A probe that explains itself is worth
having, and the explanation needs re-reading whenever the thing beneath it
moves.

## The budget, again

    frame total    6,318 x 55ms = 347,490ms

    log                133,926ms   39%
    texup               10,540ms    3%
    copyup               8,253ms    2%
    waitidle                 5ms    -
    ---------------------------------
    the recompiled game            ~56%

Everything on the graphics side is now noise. Two items remain and they are the
only two:

1. **The log, 39%.** Unchanged and unexplained. Buffering it stops the game
   booting, isolated on identical binaries, and no mechanism has been found. It
   has been the largest single cost for three days.
2. **The recompiled game, 56%.** `GUESTHOT` needs re-running — `-O2` changed
   which functions exist, so the profile that named the metafield system is
   void.

## Two days

    0.28 fps  ->  18.2 fps      65x
