# The flush was the guest thread's timeslice

Build `Sep 17 2026 18:32:42` -- the run now ends on elapsed wall clock.

    RUNRATE hostframes=13500 elapsed=239559ms hostfps=56
    LOGCOST lines=25936 log=2429ms flush=628ms
    GPUCNT  frames=1
    guest calls 2,510,759

The run lasted its full 240 seconds for the first time -- more wall clock than
the 181s the old runs actually got -- and the guest got *further behind*.

    build      wall clock   guest calls   calls/sec
    17:33:11        ~181s     3,875,018      21,409
    18:32:42         240s     2,510,759      10,478

**The guest is getting half the CPU over more time.** So the previous note was
wrong: it was not that the run got shorter. The run got longer and the guest
still lost.

## The mechanism

The game thread is created at priority 0x30 against the main thread's 0x2C, on
the same core, and the comment above `threadCreate` explains exactly why: a
tight guest loop that makes no calls and no syscalls never yields, and at equal
priority it starved the harness down to 1.5fps. The thread being observed must
not be able to starve the thread observing it.

The consequence, which nothing wrote down, is that **the game thread only runs
when the main thread is blocked.**

Until today what blocked it was `checkpoint()`'s per-line `fflush`: 3.93ms a
line, about 1.9 lines a frame, roughly 7.5ms of blocking SD I/O every single
frame. That was the guest thread's timeslice. Not by design -- by accident, as
a side effect of a diagnostic cost nobody had measured.

Buffering the log removed 60% of the run's cost and the guest thread's
scheduling window with it. Two builds reported `draws=0 modules=2` and there
was nothing wrong with either of them.

## Fix

Build `Sep 17 2026 18:45:11`. The main loop now yields 8ms explicitly, sized to
what the flush used to provide.

An accidental scheduling property that disappears when an unrelated cost is
optimised away is not something to leave implicit, and this one cost three
builds and two wrong diagnoses to find. The better answer is to stop the two
threads sharing a core, which is a larger change and wants its own measurement.

## What to read next

  * guest `calls` back above 3.8M, and above it -- the run is 240s now, not
    181s, so a like-for-like recovery is about 5.1M.
  * `GPUCNT frames=` and `INPUT vpadreads=` back to 100+.
  * `RUNRATE hostfps=` will drop, and that is fine. The harness does not need
    60fps; the guest needs CPU.
  * `LOGCOST flush=` stays at ~600ms. The 60% saving is real and keeping it is
    the entire point of doing this properly rather than reverting.

## Three wrong turns, in order

The `|=` that looked like a stuck button and was not. The 256KB of BSS that
looked like memory pressure and was not. The shortened run that looked like
lost wall clock and was not. Each was a confident, coherent explanation for a
number, and each took one measurement to kill.

The pattern is consistent enough to name: on this project, a mechanism that
explains the data is worth very little until a probe has separated it from the
other mechanisms that explain the data equally well.
