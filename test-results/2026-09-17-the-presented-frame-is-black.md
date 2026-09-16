# The presented frame is black, measured

Build `Sep 16 2026 22:29:25`.

    OK  build=Sep_16_2026_22-29-25 frames=14400 draws=245 modules=7

    PEEK      frames=84 varied=0 first=0xff000000 other=0xff000000
    SCANTGT   [1 x84] [4 x84] skipped=84
    COPYPATH  ok=84 rejected: tile=0 other=0
    DRAWPATH  tried=239 drawn=239 || every refusal counter 0

84 presented frames sampled off the GPU. Not one varying pixel. The surface
reaching the display holds a single value, `0xff000000` -- opaque black.

That is the answer the counters could not give, and it settles the split the
probe was written for. Nothing downstream of the present is losing an image,
because there is no image by then. The whole chain fixed over the last two
days -- allocation, queue survival, per-surface images, per-surface lookup,
the scan-target gate -- is correct and is faithfully carrying black.

## What this does not say

Where it went black. The frame is

    clear#2 DRAW#3 DRAW#0 clear#1 copy#0

so the scene is drawn into the 1024x576 target `#3` and composited into the
1280x720 `#0`. A flat `#0` is consistent with the scene rendering and the
composite losing it, and equally with nothing rasterising anywhere.

## Ruled out cheaply

The state feeding the draws is not missing:

    SHADERMOD  distinct=7 loaded=7 missing=0 refused=0 early=0
    SHADERS    vertex distinct=3 calls=494 | pixel distinct=4 calls=576
    UNIFREG    vs=560 ps=389 distinct=8
    FETCHATTR  n=2 (2 and 3 attributes)
    GX2CENSUS  GX2SetViewport x1159, GX2SetScissor x1159

Seven shader programs translated and loaded, none missing or refused.
Uniforms uploaded to both stages. Attribute streams set. And the viewport and
scissor shims both forward straight to `dkCmdBufSetViewports`/`SetScissors`
with a field-for-field struct match -- a zero-size viewport rejecting every
primitive would have explained a perfectly flat frame, and it is not that.

## What is being measured next

Build `Sep 16 2026 23:11:26`.

`PEEK` now samples every live surface, not just the presented one, recorded at
the present because that is the one point where each holds this frame's final
contents.

  * `#3` (1024x576) varying, `#0` (1280x720) flat -- the scene renders and the
    composite loses it. The work is the composite draw: what it samples, and
    whether the offscreen target is bound as a texture correctly.
  * both flat -- nothing rasterises at all. The work is the draws themselves:
    the translated shaders, the vertex data, the transforms. None of that has
    been tested by anything in this log yet.

One run separates two bodies of work that have almost nothing in common.
