# Nothing rasterises

Build `Sep 16 2026 23:11:26`.

    OK  build=Sep_16_2026_23-11-26 frames=14400 draws=236 modules=7

    PEEK frames=80
      [#0 1280x720 seen=80 varied=0 0xff000000/0xff000000]   TV, composite target
      [#1  854x480 seen=80 varied=0 0xff000000/0xff000000]   GamePad
      [#2 1024x576 seen=78 varied=0 0x00000000/0x00000000]   offscreen A
      [#3 1024x576 seen=78 varied=0 0x00000000/0x00000000]   offscreen B, the scene

Every live surface, sampled off the GPU across 78-80 frames. Not one varying
pixel anywhere. `#3` is the target the scene is drawn into, and it holds
`0x00000000` -- the clear colour, alpha included, untouched.

That settles the split the previous run left open. The composite is not losing
a rendered scene. **Nothing rasterises at all.**

Worth keeping: the two scan buffers read `0xff000000` and the two offscreen
targets `0x00000000`. Different clear colours, each surface holding its own --
which is independent confirmation that the per-surface cache is keeping four
distinct images rather than aliasing them.

## The shape of the problem now

The draws are well-formed and produce nothing:

    DRAWPATH tried=239 drawn=239 || noshader=0 nofetch=0 nobuf=0 badfmt=0
                                    badprim=0 nomem=0

and everything they need is present:

    SHADERMOD distinct=7 loaded=7 missing=0 refused=0 early=0
    UNIFREG   vs=560 ps=389 distinct=8
    FETCHATTR n=2
    GX2SetViewport x1159, GX2SetScissor x1159 -- both forwarded to deko3d

Present is not the same as right. Every one of those counters says a thing
exists; none says its value is correct.

## What is being measured next

Build `Sep 16 2026 23:42:44`.

`DRAWIN` captures what the first four draws were actually handed: the
primitive and vertex count, the vertex stride and attribute count, the first
vertex's leading eight words, and the vertex constant file's first sixteen --
which `UNIFREG` shows the game writing as a single block at offset 0, the
shape of a 4x4 matrix.

Every value is printed as its raw word next to a decimal. An all-zero block
and a block of NaNs both collapse every vertex to a point, and neither is
obvious from a `%f`.

  * zero or NaN constants -- the matrix never arrives, every vertex lands on
    one spot, nothing rasterises. Matches the measurement exactly.
  * sane constants, zero or garbage vertex words -- the geometry never
    arrives, and the fault is in the attribute decode or the buffer upload.
  * both sane -- the input is sound and the fault is in the translated shader
    or the pipeline state, which is a different search entirely.

Three outcomes, three different bodies of work, one run.
