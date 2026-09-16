# Four surfaces through one slot

Build `Sep 16 2026 20:52:48`.

    OK  build=Sep_16_2026_20-52-48 frames=14400 draws=160 modules=7

    DRAWPATH  tried=154 drawn=154 || every refusal counter 0
    COPYPATH  ok=110 rejected: tile=0 other=0
    RETIRE    deferred=1127 drained=1115 peak=21 leaked=0
    SETCB     kept=1 rebuilt=271
    [DKDEBUG] -- none, for the first time

154 draws recorded with no missing state, 110 presents, no validation errors
at all, and the retirement list is keeping up (gap 12, under peak 21, nothing
leaked). Draws went 2 -> 160 because the clamp stopped copies dying on a bad
rectangle.

## The measurement

`SETCBSEQ` was added to settle whether the game drives two surfaces through
one render-target slot, rather than inferring it from `kept=1 rebuilt=271`.

    SETCBSEQ n=12 clamped=106 :
      [0x25848600 1280x720 REBUILT]
      [0x25f51600  854x480 REBUILT]
      [0x25848600 1280x720 REBUILT]
      [0x25848600 1280x720 kept]
      [0x1948100  1024x576 REBUILT]
      [0x1dc9100  1024x576 REBUILT]
      [0x25848600 1280x720 REBUILT]
      [0x25f51600  854x480 REBUILT]
      [0x25848600 1280x720 REBUILT]
      [0x1948100  1024x576 REBUILT]

Not two surfaces. **Four**, and the two extra ones are the interesting pair:

  * `0x25848600` 1280x720 -- TV scan buffer
  * `0x25f51600`  854x480 -- GamePad scan buffer
  * `0x1948100`  1024x576 -- offscreen render target
  * `0x1dc9100`  1024x576 -- second offscreen render target

Two 1024x576 targets ping-ponged, then composited to the two scan buffers.
Ordinary render-to-texture. The engine is doing real rendering work and has
been all along.

All four shared slot 0, so every switch destroyed the previous surface's image
and everything drawn into it. 271 rebuilds. 106 of 110 presents had to clamp
their rectangle, meaning they presented a corner of the wrong buffer.

This is why `setcb` appears between every draw in FRAMEORD and why the frame
looked cleared: it was not one buffer being wiped, it was four buffers taking
turns in one slot.

## Fix

Build `Sep 16 2026 21:11:32`.

The image cache is keyed by the guest surface descriptor -- address, width,
height, pitch -- instead of by render-target index. 16 entries, LRU eviction,
evicted blocks going through the retirement list added yesterday rather than
being destroyed under recorded commands.

  * `GX2SetColorBuffer` on a known surface re-binds its existing image and
    skips the guest-memory upload. The upload would overwrite pixels the GPU
    drew with guest memory that has none of them -- which is precisely what
    the index-keyed path did on every switch.
  * `GX2CopyColorBufferToScanBuffer` looks up the surface it was handed rather
    than reading whatever sits at target 0. The rectangle then fits by
    construction.

The clamp stays as a backstop and still counts, so `clamped=0` is a check on
that claim rather than an assumption.

## What to read next

  * `SURFPOOL live= hit= miss= evicted=` -- `live` should settle at 4.
    Hits are switches that preserved drawn contents. `evicted>0` means 16 is
    too small.
  * `clamped=` should be 0.
  * `SETCB kept/rebuilt` should invert: mostly kept.
  * `DRAWPATH tried/drawn` and whether anything appears on screen.

Nothing here has yet shown the engine's geometry reaching the display. What it
has established is that the geometry exists, the draws record cleanly, and
until now every one of them was being thrown away by the next `setcb`.
