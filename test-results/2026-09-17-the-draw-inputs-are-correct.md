# The draw inputs are correct

Build `Sep 16 2026 23:42:44`.

    OK  build=Sep_16_2026_23-42-44 frames=14400 draws=237 modules=7

    DRAWIN[0] prim=0x5 count=4 stride=20 attribs=2
      v0: 0.000 576.000 0.000 | 0.000 1.000 | 1024.000 576.000 0.000
      vsconst[0..15]: 0.001953  0  0  0
                      0 -0.003472  0  0
                      0  0 -0.500  0
                     -1.000  1.000  0.500  1.000

    DRAWIN[1] prim=0x5 count=4 stride=20 attribs=3
      v0: 0.000 720.000 0.000 | 0.000 1.000 | 1280.000 720.000 0.000
      vsconst[0..15]: 0.0015625  0  0  0
                      0 -0.002778  0  0
                      0  0 -0.500  0
                     -1.000  1.000  0.500  1.000

Third outcome: **both sane**, and not marginally so.

## The vertices

`stride=20` is five floats, and `FETCHATTR` gives attribute 0 at offset 0 and
attribute 1 at offset 12 -- a `vec3` position and a `vec2` texcoord, 20 bytes,
exactly the stride. Vertex 0 of the first draw is position `(0, 576, 0)` with
uv `(0, 1)`; vertex 1 is `(1024, 576, 0)`. `prim=0x5` is `DkPrimitive_TriangleStrip`
and `count=4`.

A four-vertex triangle strip spanning 0..1024 by 0..576, in pixel coordinates,
with texture coordinates. A full-screen quad over the 1024x576 target. The
second draw is the same thing at 1280x720, over the TV surface.

## The matrix

Read as four rows of four:

    2/1024      0        0     0
       0    -2/576       0     0
       0        0     -0.5     0
      -1        1      0.5     1

0.001953125 is exactly 2/1024 and -0.003472 is exactly -2/576. That is a
pixel-space-to-NDC orthographic projection for a 1024x576 target, with the
translation in the last row -- the row-vector convention, which is what GX2
uses. The second draw carries 2/1280 and -2/720 for the 1280x720 surface.

Both matrices are correct for their surface, to the bit.

So the geometry and the transform that were supposed to be suspects are not
suspects. Feeding those vertices through that matrix puts the quad exactly on
the screen.

## A correction to the previous note

That note said the last run established that nothing rasterises. It did not,
and the overstatement matters because it points at the wrong work.

`PEEK` reports whether a surface holds more than one pixel value. `#0` is
cleared to `0xff000000`, and a quad that rasterised opaque black would leave
it reading `0xff000000` -- identical. `#2` and `#3` clear to `0x00000000`, and
a quad writing transparent black is likewise indistinguishable. So the honest
reading of that run is narrower: **every surface is uniform, and uniform is
consistent with both never-drawn and drawn-black.**

## What is being measured next

Build `Sep 17 2026 00:04:11`.

`GPUCNT` reports six deko3d pipeline counters at the end of each frame, which
count work the GPU did regardless of what colour came out:

    vertices  vsinv  clipin  clipout  fsinv  samples

Where the first zero falls is the answer:

  * `vertices=0` -- the draw never reached the GPU at all.
  * `vsinv=0` -- vertices fetched, vertex shader never ran.
  * `clipout=0` with `clipin>0` -- every primitive clipped away. Given the
    matrix above is correct, that would point at the translated shader not
    applying it as intended.
  * `fsinv=0` with `clipout>0` -- primitives survived and produced no
    fragments: degenerate, back-face culled, or a zero-area viewport.
  * `samples=0` with `fsinv>0` -- fragments ran and were all discarded, by
    depth, stencil, or a discard in the shader.
  * all non-zero -- the GPU drew, and the black is what it drew. Shading,
    blending, or the channel mask, and `GX2SetTargetChannelMasks` is called
    240 times a run.

This cannot confuse "never drawn" with "drawn black", which is the ambiguity
the pixel readback has.
