# Loading works. The game never asks for a level.

Build `Sep 17 2026 16:40:24`.

    IGZSTATE update calls=6 max=kStateFailed(9) exits=
      [stream1 kStateFailed(9)   ret=0x1 iters=1]
      [stream1 kStateFinished(7) ret=0x0 iters=5]
      [stream2 kStateFailed(9)   ret=0x1 iters=1]
      [stream2 kStateFinished(7) ret=0x0 iters=5]
      [stream3 kStateFailed(9)   ret=0x1 iters=1]

    STREAMLOAD calls=3 [permanent0/bootstrap] [item0/legal] [permanent0/global]

Paired, and by stream. Every failure is followed by a success **on the same
stream**: one attempt bails immediately without leaving state 0, the next walks
0 1 3 5 7 and finishes.

That is try-then-fall-back, and it settles the question the previous run left
open. The three failures are routine probing, not three real failures hidden by
retries. **There is no loading bug.** The engine boots its three archives
exactly as designed, and then never requests anything else.

So the state of the port is:

  * the graphics pipeline is correct end to end, established over five fixed
    bugs and a GPU-counter readback
  * every draw is a full-screen composite quad, so no scene geometry exists
  * the archive loader works and has loaded bootstrap, legal and global
  * no level is ever asked for

The black screen is downstream of all of it. Nothing is broken at the point
where it shows.

## A wrong turn, recorded

main.c synthesises an A press for six frames out of every 180, and
`g_arkchemy_vpad.held |= 0x8000u` is the only write to that field in the file.
That reads as a button that is pressed once and never released -- which would
jam any edge-triggered menu, and would be a neat explanation for a game sitting
on a title screen.

It is not one. `arkchemy_vpad_update(&pad)` runs twenty lines earlier and
*assigns* `g_arkchemy_vpad.held` from the real controller, so the field is
rebuilt from scratch every frame and the `|=` only adds to that frame's value.
Real input is mapped, the synthetic pulse is a clean press-and-release, and
`VPADRead` derives trigger and release correctly from it.

Worth writing down because the grep that found the `|=` looked conclusive on
its own, and acting on it would have "fixed" working code.

## What is being measured next

Build `Sep 17 2026 16:53:34`.

`INPUT vpadreads= nosample= held_any= a_seen= kpadreads=`.

A title or legal screen waiting on a button is the obvious reason to boot three
archives and stop, and none of it matters if the game never reads the pad.

  * `vpadreads=0` -- it never asks. Input is not what it is waiting on, the
    synthetic A is irrelevant, and the question moves to what else gates the
    next load.
  * `vpadreads` high with `a_seen>0` -- it is handed a press every three
    seconds and does nothing with it, which is a different problem and a much
    more specific one.
