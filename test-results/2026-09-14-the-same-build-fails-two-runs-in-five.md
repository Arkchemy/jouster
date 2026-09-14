# The boot failure is intermittent, and the same build shows both outcomes

> **Corrected the same day.** This document calls the failure a stall and says
> the app is not hung but the engine is. It is neither: the process is killed
> by `svcBreak` from deko3d, inside `GX2Flush`'s submit, on the graphics
> submit thread. See
> `2026-09-14-the-intermittent-failure-is-a-deko3d-abort.md`. The run table
> and the ~40% rate below are measurements and still stand; the description
> of what the failure *is* does not.

Runs of 2026-09-13 18:08 through 2026-09-14 01:51, from the loop rig.

The 826-call boot failure is **not a property of any build**. The same binary,
relaunched by `arkchemy_loop_continue`, produces both outcomes:

| Time | Cycle | GX2 calls | Draws | Shaders | Exit |
| --- | --- | --- | --- | --- | --- |
| 18:08 | 1 | 34,680 | 589 | 7 | clean |
| 18:27 | 2 | 27,656 | 483 | 7 | clean |
| 18:41 | 3 | 30,333 | 523 | 7 | clean |
| 19:09 | — | **826** | **0** | **2** | **none** |
| 19:23 | — | **826** | **0** | **2** | **none** |
| 19:47 | 4 | 30,763 | 529 | 7 | clean |
| 21:04 | 5 | 27,254 | 477 | 7 | clean |
| 21:17 | — | **826** | **0** | **2** | **none** |
| 00:34 | — | **826** | **0** | **2** | **none** |
| 00:43 | 6 | 29,691 | 513 | 7 | clean |
| 01:31 | 7 | 28,083 | 489 | 7 | clean |
| 01:51 | — | **826** | **0** | **2** | **none** |

Seven completed, five failed. There is no middle outcome in any run on record:
a run either reaches ~28,000 GX2 calls and ~500 draws, or it reaches 826 and
zero.

## Why these are the same build

* Every completed run ends `LOOP cycle N done -- relaunching the same build`,
  which is the branch of `arkchemy_loop_continue` that chain-loads the NRO
  already on the card.
* The cycle numbers are **contiguous, 1 through 7**. A failed run never reaches
  `arkchemy_loop_continue`, so it never increments `loop-count.txt` — which is
  why the failures carry no cycle number and do not interrupt the sequence.
* Cycles 1-4 span the two failures at 19:09 and 19:23 with no park between them.
  `park` defaults to 5 and only triggers on multiples of it, so nothing could
  have replaced the binary in that window. **Four successes and two failures,
  one binary, no intervening write to the card.**
* The courier's queued builds are all named `-pending` and identical in size
  (177,219,090 bytes) from 21:12 onward, consistent with none of them landing.
  The loop keeps the console out of hbmenu and MTP only comes up there — which
  `self_update.c` already says in as many words.

## The failure signature, which is identical every time

```
GX2CENSUS total=826 distinct=56       (a good run: ~28,000, distinct=72)
SHADERMOD distinct=2 loaded=2         (a good run: 7)
DRAWPATH  tried=0 drawn=0 || noshader=0 nofetch=0 nobuf=0 badfmt=0 badprim=0 nomem=0
ARCHNAME  n=1 [permanent/bootstrap.bld]
DEVLOG    open=1 reachedRegister=1 close=1
fs: open=3 ard=3/199237  rdq: sz=67623 fsz=198695
asyncq: q=3 done=3 drop=0 pend=0
threads: created=13 started=13        (a good run: 15)
```

A good run instead opens four archives — `permanent/bootstrap.bld`,
`item/legal.bld`, `permanent/global.arc`, `permanent/global.bld` — and exits on
`exiting after 14400 main frames`.

Two things worth reading carefully:

**It is not hung.** The main frame counter is still advancing where the log
stops: `main frame 12300/14400`, then `12360/14400`. The render loop runs and
presents. It is the *engine* that is stuck, not the app, and the failure is
that the archive system stops advancing after the first file.

**No I/O is outstanding.** `asyncq q=3 done=3 pend=0` and `cb: ok=3 skip=0` —
every async read issued has completed and been delivered. `rdq` shows the
request it is sitting on is the tail of the boot archive: 198,695 − 131,072 =
**67,623 bytes**, exactly the `sz` reported. So this is not the 2026-09-09
undrained-completion bug returning. The read completed and nothing consumed it.

## What this costs

`cafeos_gx2_draw.h` records eight builds narrowing a boot failure to a single
call to `dkCmdBufDraw` from the shim, with the oddities spelled out: linking
the symbol is fine, taking its address is fine, 752 bytes of dead code is fine,
`dkCmdBufDrawIndexed` from the same object file is fine, direct and indirect
calls behave alike, one translation unit behaves like two hundred.

**That is what a 40%-failure rig looks like when each build is run once.** A
single run per build condemns a good build two times in five and clears a bad
one three times in five, and the pattern of results that produces is exactly
the pattern recorded — a conclusion that survives every attempt to narrow it
because there is nothing there to narrow.

This does **not** show that `dkCmdBufDraw` is innocent. It shows the evidence
on record does not establish that it is guilty. The deferred-queue workaround
in `main.c` currently draws ~500 times a run and should stay exactly as it is
until something better than a single run says otherwise.

## Next

Two things, in this order, and the first is worth more than any build.

1. **Make the rig repeat.** Nothing on this project should be bisected against
   a single run again. The loop already relaunches the same binary; what is
   missing is that its per-run outcome is not summarised anywhere a person
   reads. A pass/fail line per cycle, appended to one file on the card, turns
   `n=1` anecdotes into a rate.
2. **Then re-run the `dkCmdBufDraw` question properly** — direct call in the
   shim, N cycles, against the deferred build, N cycles. If both come back near
   60% the call was never the problem and the workaround can be dropped along
   with the mystery. If the direct build fails 100% of N, the finding stands
   and is now actually supported.

The intermittent failure itself is the larger prize and is unexplained. What is
established about it: it lands on the tail read of the boot archive, with that
read already complete and delivered, two guest threads short of a healthy run.
