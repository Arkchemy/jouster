# The log cost is gone, and the frame budget ate it

Same binary, `flush=256 buf=32768`.

    LOGCOST  log=4429ms flush=884ms      (was 138,775ms / 137,422ms)

**31x.** The largest single cost in this project since Tuesday, 39% of every
run, reduced to just over one percent. The buffering works and the game boots.

And the run got worse:

                        flush=1      flush=256
    LOGCOST log      138,775ms        4,429ms
    RUNRATE elapsed  393,845ms      266,142ms
    game frames          6,318          3,969
    draws               12,607          8,095
    frame_avg             55ms           56ms

`frame_avg` is unchanged, so the game did not slow down. Draws per frame are
2.00 and 2.04 — identical behaviour. The guest was simply handed a third less
wall clock, because 14,400 host frames now elapse in 266 seconds instead of
394.

## The same trap, twice

The run had been bounded by elapsed time precisely to stop this. It was
switched back to a frame count on 2026-09-17 so the buffering bisection would
match the last known-good build exactly, and then left that way.

A frame-count budget converts every optimisation into a shorter run rather than
more progress. Saving 130 seconds of blocking meant the loop finished its
14,400 frames sooner and the guest got less done. That is backwards, it is
documented as backwards in this very directory, and it caught the same project
twice in two days.

Worth naming the actual failure: the revert was deliberate and correct for the
bisection, and nothing tracked that it needed undoing once the bisection ended.
A temporary change with no expiry attached.

## Build `Sep 18 2026 14:02:25`

  * wall-clock bound restored
  * `flush=256` and `buf=32768` are the built-in defaults now, both still
    overridable from the card
  * the card override files are deleted, so this run exercises the defaults

Expect roughly: 240 seconds of run, the log at about 1% instead of 39%, and
the guest getting the full budget for the first time since the fast
configuration existed.

## A near miss worth recording

The first attempt at this edit truncated `main.c` from 7,183 lines to 4,395 —
a python slice that rebuilt the file as `s[:i] + new + s[j:end]` and silently
discarded everything after the loop. The compiler caught it immediately
(`expected declaration or statement at end of input`) and `git checkout` put it
back, because the previous work was committed.

The lesson is the cheap one: targeted string replacement with an assertion on
the match count, never index slicing on a file being edited in place.
