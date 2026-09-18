# Fence rotation — built and deployed, unrun

Build `Sep 18 2026 02:28:15`. On the card, hash-verified, **not yet run.**

## What changed

`GX2SwapScanBuffers` used to end with an unconditional `dkQueueWaitIdle`,
stopping the CPU dead until the GPU had finished the frame, purely so a single
64KB command block could be reused safely. Its own comment called that

> a real, deliberate simplification ... a real, known place to come back to
> once double-buffered command memory is worth the added complexity.

It became worth it. Measured on build `01:04:29`: 39,572ms of a 352,718ms run,
**11%**, second only to the log. It had not got slower — the guest went from
3,263 frames a run to 5,689 and each one pays the wait once.

Now three command-memory blocks rotate, each with its own fence. A frame
signals its fence, the slot advances, and the CPU waits only on the fence of
the slot it is about to reuse — the frame from a full rotation ago. If the GPU
has kept up, that fence is long since signalled and the wait costs nothing.

## Two things that were quietly leaning on the queue wait

  * **The retirement list.** Now per-slot. Blocks retired during a frame stay
    in that slot's bucket and are freed when the rotation returns to it, behind
    its fence. Draining everything each frame would have freed memory the GPU
    had not finished reading — the exact bug class that killed the queue on
    2026-09-16.
  * **`retired_timestamp`.** Now reports the frame whose fence just passed, not
    the one submitted moments ago. The old assignment would have
    `GX2GetRetiredTimeStamp` claim work was complete that the GPU had not
    started.

And one thing that is now less exact, stated rather than hidden: `PEEK` and
`GPUCNT` read memory the GPU writes, and the queue wait was silently what
guaranteed those writes had landed. They sit behind the fence now, so their
values **lag by up to three frames**. Immaterial for "did any fragment run",
but it is a lag.

## What to check first, next session

In this order, because the second only means something if the first holds:

1. **`draws` ≈ 11,500 and `modules` = 7.** A wrong fence shows up as a hang or
   corruption, not a slow frame. This matters more than any speed number.
2. **`TIMING waitidle`** against 39,572ms. It should collapse toward nothing.
3. `RUNRATE elapsed`, `GPUCNT frames` against 5,689, `frame_avg` against 62ms.

If it hangs, `ARKCHEMY_GX2_CMD_SLOTS` is the first dial — dropping to 2, or
reverting to the unconditional wait, isolates whether the fences or the
rotation are at fault.

## Where the run stood going in

    0.28 fps  ->  16.1 fps over two days
    log        38%   blocked on the buffering phenomenon
    waitidle   11%   this change
    uploads     5%   done
    guest      46%   re-profile with GUESTHOT; -O2 voided the old one
