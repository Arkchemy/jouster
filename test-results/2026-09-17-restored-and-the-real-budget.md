# Restored, and the first honest look at the budget

Build `Sep 17 2026 22:45:26` -- unbuffered default, frame-bounded, no yield.

    OK  build=Sep_17_2026_22-45-26 flush=1 frames=14400 draws=303 modules=7

    SURFPOOL live=4 hit=506 miss=4 evicted=0
    TEXSRC   rendertarget=203 guest=203
    DRAWSIZE quad=296 small=0 mid=0 big=0 max=4
    GPUCNT   frames=103 vertices=1184 clipin=592 clipout=592
             fsinv=239284224 samples=152653824
    INPUT    vpadreads=102
    RUNRATE  hostframes=14340 elapsed=365874ms hostfps=39
    LOGCOST  lines=28089 log=113866ms flush=108447ms
    TIMING   frames=102 frame_avg=1801ms | copyup=7000 texup=8838
             setcbup=188 waitidle=788

Everything the graphics work established still holds: four surfaces each
keeping their own image, no evictions, every draw a full-screen composite quad,
and the geometry reaching the screen and passing the depth test.

## A number this project has never actually had

`RUNRATE` measures the run directly for the first time: **365,874ms**. Six
minutes, not the four that "240 seconds" implied and not the 183s that
`frames x frame_avg` implied -- that figure is the span between the first and
last present, not the run.

So every earlier estimate of this project's own runtime was wrong in both
directions, and 102 game frames in 366 seconds is **0.28 fps**.

## The budget, measured

    log      113,866ms   31%
    uploads   16,814ms    5%
    ---------------------------
    the rest             64%

The logging cost is back, and it has to be: buffering it is what stops the game
booting. So a third of every run is being spent on instrumentation that cannot
currently be removed, and that is worth stating plainly rather than burying.

But it is also worth keeping in proportion. Deleting the log **and** every
upload loop takes 0.28 fps to about 0.44. It does not reach a level. The two
thirds nobody has looked at is the only part big enough to matter, and the only
thing in it is the recompiled game.

## What is being measured next

Build `Sep 17 2026 23:28:48`.

`GUESTHOT` samples `g_ppc_current_pc` once per host frame -- about 14,000
samples a run, from a thread the guest does not synchronise with, which is what
a sampling profiler wants -- and reports the hottest functions as a percentage.

Two things stated so the result is not over-read:

  * `g_ppc_current_pc` is set on function entry, so a hit names the function
    the guest was last inside, not an instruction. Right granularity for "where
    does the time go", wrong one for "which line".
  * `overflow` counts samples that arrived after the 64-slot table filled. A
    non-zero value means the profile is spread wider than the table and the
    top-12 is not the whole picture.

A concentrated profile names the thing to fix. A flat one says the cost is
spread across the translation itself, which is a different and much larger
piece of work -- and knowing which of those it is, before starting either, is
the entire point.

## Still open

Why buffering the log stops the game booting. Blocking-time on the main thread
is not the mechanism: 14,340 frames of 8ms sleep is 115 seconds, almost exactly
the 108 seconds of flushing it replaced, and it changed nothing. Something else
about writing to the card frequently matters to the guest, and it is parked
rather than solved.
