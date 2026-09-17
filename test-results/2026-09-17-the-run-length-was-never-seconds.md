# The run length was never seconds

Build `Sep 17 2026 18:25:22` -- the same buffering, an eighth of the memory.

    LOGCOST lines=27294 log=2534ms flush=663ms
    GPUCNT  frames=1
    tally   draws=0 modules=2

The flush is cheaper still than the 256KB build's 1,243ms, and the boot is
just as short. **The BSS hypothesis is dead.** A quarter megabyte against a
gigabyte was a plausible suspect and it was the wrong one.

Buffering itself is not breaking anything either. The run is.

## What is actually happening

    const int GAME_TEST_AUTO_EXIT_FRAMES = test_seconds * 60;
    while (appletMainLoop() && frame < GAME_TEST_AUTO_EXIT_FRAMES)

`test-seconds.txt` says 240. The budget is written in seconds and spent in
frames, on the assumption the loop runs at 60fps.

It never has. The last well-behaved run put 14,400 frames at 103 game frames
of 1784ms, which is 181 seconds of wall clock -- so "240 seconds" has always
meant about 181, and the loop was running at roughly 80fps because the per-line
SD flush was pacing it.

Take 109 seconds of blocking out of the loop and the same 14,400 frames elapse
in a fraction of the time. The guest thread gets a fraction of the wall clock
to boot in, so it boots a fraction as far -- 2.68M guest calls against 3.87M,
no archive opened, one presented frame.

Nothing regressed. The game did exactly what it always does and was given less
time to do it, and the frame budget hid that by measuring the host loop instead
of the clock.

A frame-count budget rewards a slow host loop and punishes a fast one. That is
backwards on its own, and it makes every run before and after an optimisation
incomparable -- which is precisely the comparison this project is about to
start making.

## Fix

Build `Sep 17 2026 18:32:42`. The run ends on elapsed wall clock. The frame
count stays for the dumps, the spinner and the stall timeout, which are all
genuinely per-frame.

`RUNRATE hostframes= elapsed= hostfps=` reports what the loop actually does,
so this cannot hide again.

## What to read next

  * `RUNRATE hostfps=` -- around 80 was the old pacing. Much higher now is the
    buffering fix showing up where it belongs.
  * `GPUCNT frames=` and `INPUT vpadreads=` should return to 100+ and beyond,
    since the guest gets the full 240 seconds for the first time.
  * `TIMING frame_avg=` is then the first game frame time measured without 60%
    instrumentation overhead in it, over a run that lasted as long as it said.

## Worth keeping

Two wrong turns in two builds, both caught by measurement rather than
reasoning: the missing `arkchemy_vpad_update` that turned out to be twenty
lines above the `|=`, and the 256KB of BSS that turned out to be irrelevant.
Both looked conclusive. The thing that has consistently worked here is
changing one variable and reading the number, and the thing that has
consistently failed is a confident explanation for a number nobody measured.
