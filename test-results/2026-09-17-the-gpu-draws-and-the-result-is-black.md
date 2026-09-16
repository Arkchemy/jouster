# The GPU draws. The result is black.

Build `Sep 17 2026 00:04:11`.

    OK  build=Sep_17_2026_00-04-11 frames=14400 draws=266 modules=7

    GPUCNT frames=90 vertices=1028 vsinv=1028 clipin=514 clipout=514
                     fsinv=207654912 samples=133005312

Every stage non-zero, and the numbers are internally consistent:

  * 1028 vertices, 1028 vertex shader invocations -- every vertex shaded.
  * 514 primitives into the clipper and **514 out**. Nothing clipped away.
    1028 / 4 vertices = 257 draws, 257 x 2 triangles = 514. Exact.
  * 133,005,312 samples over 90 frames is 1,477,837 a frame. A full-screen
    quad on both surfaces is 1280x720 + 1024x576 = 1,511,424. Within 2%.

So the transform puts the geometry on screen, the rasteriser produces a
full-screen quad's worth of fragments per surface per frame, and the samples
pass the depth test. The GPU is drawing. **What it draws is black.**

That is the last branch of the five GPUCNT was written to separate, and it
rules out the four that would have pointed at the geometry, the transform, the
clipper, the viewport and the depth test.

## The archive moved too

Unrelated to the graphics work, and worth recording because every earlier log
had these at zero:

    INFLATE  lzma n=139 ret=0x0 | ok=139 fail=0
    IGZLOAD  entries=6 results=[ret=0x0 ...] | igArchive::open rets: 0x0 0x0 0x0
    ARCHPUMP updateArchiveSystem=148 updateTasks=148 startNewTasks=148
             startBlockRead=145 decompressBatch=135 addWork=23

139 successful LZMA decompressions and six igz loads returning 0. The boot
stall that dominated this project for weeks is not where it was.

## What is left

`samples passed` counts the depth and stencil test. It says nothing about
whether the colour write happened. Three candidates remain, all on the colour
path:

  1. The effective channel mask is 0. Every fragment shaded, every sample
     passed, nothing written -- the exact shape of the reading above.
  2. The textures being sampled are uniform black, so a correct composite
     produces black and the fault is upstream of the draw.
  3. Blending or the alpha test discards the result.

`GX2SetPixelTexture` is called 438 times across 260 draws and the quads carry
texture coordinates, so every draw samples something. `GX2SetTargetChannelMasks`
is called 260 times -- once per draw.

## What is being measured next

Build `Sep 17 2026 00:18:35`.

  * `DRAWIN[n] colour state:` -- `channel_masks`, `color_write_enable`, and the
    `effective` mask handed to deko3d, per draw. `effective=0` is candidate 1,
    outright.
  * `TEXUP n= flat=` -- every texture uploaded for sampling, and whether its
    bytes hold more than one value. Checked on the CPU side of the upload, over
    data already being walked, so it costs nothing and needs no GPU work.
    `FLAT 0x00000000` is candidate 2.

If the mask is intact and the textures have content, candidate 3 is what
remains and the blend and alpha-test state are next.
