# Block reads: another 1.7×, and the wait-idle surfaces

Build `Sep 18 2026 01:04:29`.

    OK  build=Sep_18_2026_01-04-29 flush=1 frames=14400 draws=11544 modules=7

                      -O2 (00:16)   block-read (01:04)
    game frames             3,263            5,689    1.74x
    frame_avg               104ms             62ms    1.68x
    draws                   6,672           11,544    1.73x
    texup                41,316ms          9,528ms    4.3x faster
    copyup               36,148ms          7,745ms    4.7x faster
    waitidle             22,670ms         39,572ms    <- now the largest

**9.6 fps to 16.1 fps.** Against the `-O0` baseline of two days ago, 0.28 fps
to 16.1 — **58x**.

Sound, not a miscompilation: `modules=7`, `SURFPOOL live=4 miss=4 evicted=0`,
`DRAWSIZE quad=11495 max=4`. The `memcpy` is byte-for-byte what the loop did.

`GPUCNT` prints real numbers now rather than two saturated `0xFFFFFFFF` —
8,706,981,888 fragments and 8,595,468,288 samples. The widening was worth
doing at the same time.

## The budget again, and it has moved again

    frame total    5,689 x 62ms = 352,718ms

    log                135,076ms   38%
    waitidle            39,572ms   11%
    texup                9,528ms    3%
    copyup               7,745ms    2%
    ---------------------------------
    the recompiled game            ~46%

The upload loops are done: 23% down to 5%, and 60 seconds of a run returned.
Further work on them would now be chasing 5%.

**`dkQueueWaitIdle` is the new one.** It did not get slower -- it went from
22.7s to 39.6s because there are now 5,689 frames a run instead of 3,263, each
paying one unconditional wait. Its own comment in `GX2SwapScanBuffers` calls it

> a real, deliberate simplification: waits for the GPU to fully finish the
> frame just presented before reusing its command memory, trading real
> pipelining/performance for straightforward correctness at this "does it work
> at all yet" stage -- a real, known place to come back to once double-buffered
> command memory is worth the added complexity.

It is worth the added complexity now. That is the third time in three days a
note saying "come back to this when it matters" has turned out to be right on
schedule.

## Next, in order

1. **The log, 38%.** Still blocked on the buffering phenomenon and still the
   largest single cost by a wide margin.
2. **The wait-idle, 11%.** Double-buffered command memory removes the need to
   wait before reusing it.
3. **The guest, 46%.** Worth re-profiling with `GUESTHOT` -- `-O2` changed
   which functions exist, so the previous profile is void.

Still every draw a four-vertex quad. No scene geometry yet, but the run now
gets 55x more boot progress than it did on Tuesday.
