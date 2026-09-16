# Every texture the game samples is empty

Build `Sep 17 2026 00:18:35`.

    OK  build=Sep_17_2026_00-18-35 frames=14400 draws=213 modules=7

    DRAWIN[0] channel_masks=0x00000007 color_write_enable=1 effective=0x00000007
    DRAWIN[1] channel_masks=0x0000000f color_write_enable=1 effective=0x0000000f

    TEXUP n=284 flat=284:
      [1024x576 FLAT 0x00000000/0x00000000]  (x6 shown, 284 total)

Two candidates went in. One came out.

## The channel mask is fine

`0x7` is RGB, `0xf` is RGBA, writes enabled, and the effective mask handed to
deko3d is the same in both cases. Nothing is being masked off. Candidate 1 is
dead, and it was the one that fit the GPUCNT shape most neatly.

## Every texture is zero

284 textures uploaded in the run. **All 284 uniform `0x00000000`.** And every
one is 1024x576 -- the exact size of the two offscreen render targets the scene
is drawn into, `#2` at `0x1948100` and `#3` at `0x1dc9100`.

So the engine is doing textbook render-to-texture: draw the scene into a
1024x576 target, bind that target as a texture, composite it onto the scan
buffer with a full-screen quad. The quads, the matrices and the texture
coordinates measured over the last two runs are exactly that composite.

And the texture bind throws the rendered image away.

`arkchemy_gx2_set_texture` builds a fresh deko3d image and uploads the guest
surface's bytes into it. For a render target those bytes are zero and always
will be: the recompiled code does not rasterise, so nothing on the guest side
ever writes them. The pixels are in the surface cache's deko3d image, put there
by the GPU.

That is why the black survived every other fix. The colour path was correct
throughout -- masks intact, 207M fragments shaded, 133M samples passed -- and
it was faithfully compositing an all-zero texture the entire time.

## The same mistake, one step further along

This is `GX2SetColorBuffer`'s bug of this morning, moved down the frame. That
one re-uploaded guest memory over a rendered colour buffer on every re-bind,
and the surface cache fixed it by keying images to surfaces and skipping the
upload on a known one. This one does the identical thing on every texture
bind.

Both come from the same wrong assumption: that the guest surface is the source
of truth for memory only the GPU ever writes.

## Fix

Build `Sep 17 2026 00:32:20`.

`arkchemy_gx2_set_texture` looks the incoming surface up in the colour surface
cache first. On a hit it points the image descriptor at that image -- the one
the GPU rendered into -- and binds it, with no image creation and no upload. On
a miss it does exactly what it did before, which is right for a genuine
CPU-side texture.

`TEXSRC rendertarget=N guest=N` counts the split.

## What to read next

  * `TEXSRC rendertarget=` should be most of them, and `TEXUP n=` should drop
    by the same amount -- textures served from the cache never reach the
    upload, so they never reach that counter either.
  * `PEEK` on `#0`, the composite target, should stop being uniform.
  * `GPUCNT` should stay as it is. The GPU was already drawing; the question
    was only what it was drawing from.

If `#3` is still uniform, the scene is not being rendered into it in the first
place and the composite has nothing to carry -- which would be the next thing
to chase, and a different one.
