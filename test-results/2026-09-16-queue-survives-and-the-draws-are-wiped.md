# The queue survives. Now the draws are being wiped.

Build `Sep 16 2026 20:33:02` -- the deferred-destruction fix.

## Result

    OK  build=Sep_16_2026_20-33-02 frames=14400 draws=2 modules=7

A complete 14,400-frame run, and the tally's previous line records the
20:13:14 crash it replaced:

    FAIL  build=Sep_16_2026_20-13-14

The guest ran the whole way. Its call counter climbed steadily to the end --
3,360,490 at frame 14,340, against 1,711,253 frozen at frame 7,380 in the run
before. No page fault. One `[DKDEBUG]` line in the entire log, and it is not a
fault.

    RETIRE deferred=22 drained=9 peak=13 leaked=0

`deferred - drained` is 13, exactly `peak`, so that is the frame in flight and
not a leak. `leaked=0`: 64 slots is more than a frame recycles.

The use-after-free is fixed, and the diagnosis it rested on is confirmed.

## What the run then showed

    DRAWPATH  tried=2 drawn=2 || noshader=0 nofetch=0 nobuf=0 badfmt=0 badprim=0 nomem=0
    COPYPATH  ok=5 rejected: tile=0 other=0
    SETCB     kept=1 rebuilt=7
    FRAMEORD  setcb setcb clear setcb clear copy copy swap
            | copy copy swap
            | setcb setcb clear setcb DRAW setcb DRAW setcb clear copy

Draws reach deko3d for the first time with no piece of state missing: two
attempted, two recorded, every refusal counter zero.

And in the same frame, a `setcb` sits between each pair of draws, and a
`clear` lands after all of them, before the copy. Whatever those two draws
put in the buffer is gone before it is presented.

`draws=2` against 489-589 in earlier builds is not a regression in the draw
path. Those builds refused every copy at the tile check and never ran the
upload; this one performs a full 3.7 MB per-pixel upload and a memory-block
create per copy, so the engine got through three GX2 frames where it used to
get through hundreds of cheap no-ops. The guest kept running the whole time.

## The one error

    [DKDEBUG] deko3d raised DkResult_BadInput (7) in 'dkCmdBufCopyImage':
              dk_image.cpp:520: srcRect x/width out of bounds

The copy rectangle is sized from the guest surface being presented and the
framebuffer. The image actually read is `color_target_image[0]`, whose size is
whatever `GX2SetColorBuffer` last bound. Those are not the same thing.

## What is being measured next

Build `Sep 16 2026 20:52:48`.

`kept=1 rebuilt=7`, a setcb between every draw, and a source image too small
for the rectangle all point one way: the game driving two surfaces -- 1280x720
TV and 854x480 GamePad -- through the single slot 0, each rebuild discarding
the other's contents. That is a reading of aggregate counters, not a
measurement, and this project has paid for that mistake before.

So `SETCBSEQ` records the actual sequence: guest address, size, and kept or
rebuilt, for the first twelve calls. Alternating addresses with REBUILT on
each confirms it. Anything else rules it out, and the setcb-between-draws
needs a different explanation.

The rectangle is also clamped to the source image now, with `clamped=`
counting it. That stops the validation error, and deliberately does not
pretend to fix anything: a clamped copy presents a corner of the wrong
buffer. It is there so one broken copy cannot hide the sequence being
measured.

## What to read next

  * `SETCBSEQ n= clamped= : [addr WxH kept|REBUILT] ...`
  * whether `[DKDEBUG]` appears at all
  * `DRAWPATH tried/drawn` -- still the count of draws that reached deko3d
