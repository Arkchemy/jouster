# The present chain is complete

Build `Sep 16 2026 22:11:34`.

    OK  build=Sep_16_2026_22-11-34 frames=14400 draws=248 modules=7

    SCANTGT   [1 x84] [4 x84] skipped=84
    COPYPATH  ok=84  rejected: tile=0 other=0     (was 156)
    DRAWPATH  tried=240 drawn=240 || every refusal counter 0
    SURFPOOL  live=4 hit=413 miss=4 evicted=0
    [DKDEBUG] none

`SCANTGT` was written so the enum would be checked rather than trusted, and it
checked out: two values arrive, 1 and 4, in equal numbers, and exactly the 84
against 4 were dropped. wut's `GX2_SCAN_TARGET_TV = 1` is right.

`COPYPATH ok` halving from 156 to 84 is the intended effect.

FRAMEORD confirms the GamePad copy no longer reaches the swapchain -- it has
no `#n` because it returns before the surface is ever looked up:

    setcb#0 setcb#2 clear#2 setcb#3 DRAW#3 setcb#0 DRAW#0 setcb#1 clear#1
    copy#0 copy swap#0

Every step from the guest asking GX2 to size a buffer through to a single
composited frame reaching the display is now measured and accounted for:

  * buffers are allocated (the tiling fix)
  * the queue survives a frame (deferred destruction)
  * four surfaces each keep their own image and contents (the surface cache)
  * each present reads the surface it was handed (the per-surface lookup)
  * only the TV scan buffer is presented (the scan-target gate)

## The question that is left

Whether the pixels in that frame are an image.

No counter can answer it. 240 draws recorded with nothing missing says the
draw calls were well-formed, not that they rasterised anything visible: the
shader translation, the vertex formats and the transforms have never been
tested by anything in this log.

So there are two live explanations, needing completely different work:

  1. The GPU draws an image and something downstream still loses it.
  2. The GPU draws nothing visible, and the frame presented is a clear colour.

## What is being measured next

Build `Sep 16 2026 22:29:25`.

`PEEK` reads the pixels back. A 64x64 patch from the centre of the surface
being presented, recorded as `dkCmdBufCopyImageToBuffer` into the same command
list as the present blit -- so it samples exactly what the display gets -- and
read on the CPU after the `dkQueueWaitIdle` that follows the present.

Centre, not corner: a frame with a letterbox or a cleared border looks uniform
at the edges and would say nothing.

  * `varied>0` -- the GPU is drawing an image. Explanation 1, and the work is
    downstream of the draws.
  * `varied=0` with `frames>0` -- every presented frame is one flat colour, and
    `first=` names it. Explanation 2, and the work is the shader translator and
    the vertex data.
  * `frames=0` -- the readback never ran, which would mean the presented
    surface is smaller than 64px or the copy is not reached.
