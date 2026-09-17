# -O2 is a 17× speedup

Build `Sep 18 2026 00:16:58`. 33 minutes to compile, 205MB against 177MB.

    OK  build=Sep_18_2026_00-16-58 flush=1 frames=14400 draws=6672 modules=7

                        -O0 (23:28:48)   -O2 (00:16:58)
    game frames                    103            3,263    31.7x
    frame_avg                   1801ms            104ms    17.3x
    draws                          314            6,672    21.2x
    SURFPOOL hit                   506           16,304
    run length              365,874ms        383,558ms     comparable

**0.28 fps to 9.6 fps.**

## It is not a miscompilation

Which was the real risk, and the thing to check before any speed number.
`modules=7`. `SURFPOOL live=4 miss=4 evicted=0` — still exactly four surfaces,
each found in the cache every time after its first sight, nothing evicted.
`DRAWSIZE quad=6643 max=4`, so the draws scaled proportionally rather than
changing shape. Every graphics invariant established over the last three days
held, at 21× the volume.

217 machine-translated translation units came through `-O2` intact.

## The budget has completely inverted

    frame total   3,263 x 104ms  = 339,352ms

    log                124,611ms   37%
    texup               41,316ms   12%
    copyup              36,148ms   11%
    waitidle            22,670ms    7%
    setcbup                 26ms    -
    ---------------------------------
    the recompiled game            ~33%

Yesterday the guest was 64% and the per-pixel upload loops were 5%, and the
note said they were "worth doing, worth doing after the things that are". They
now *are* the things that are: the guest got 17× faster and the loops did not
move, so they went from 5% to 23%, and `dkQueueWaitIdle` from under 1% to 7%.

So the next levers, in order and now quantified:

1. **The log, 37%.** Blocked on the buffering phenomenon, which is exactly as
   unexplained as it was and now costs more than anything else.
2. **The upload loops, 23%.** Byte-at-a-time `ppc_load_u8` over 1280×720×4 and
   1024×576×4. Genuinely worth rewriting now.
3. **The unconditional wait-idle, 7%.** One `dkQueueWaitIdle` per frame, a
   documented simplification from when frames were rare. At 3,263 a run it is
   not free any more.

## A probe outgrew itself

    GPUCNT fsinv=4294967295 samples=4294967295

Both exactly `0xFFFFFFFF`. That is the saturating clamp doing its job and the
width being wrong: the comment said "32 bits is plenty for *did this ever
happen*", which was true at 103 frames a run and is not at 3,263. Widened to
64-bit. The clamp is why this reads as an obviously-wrong round number rather
than a plausible wrong one, which is the entire argument for saturating instead
of wrapping.

## Still no level

`DRAWSIZE quad=6643 small=0 mid=0 big=0 max=4`. Every draw is still a
four-vertex full-screen composite quad, so no scene geometry has been submitted
yet. 9.6 fps is 34× more boot progress per run than before, and the question of
whether that is enough to reach a level is now worth asking directly rather
than assuming either way.
