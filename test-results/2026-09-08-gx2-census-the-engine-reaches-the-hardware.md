# GX2CENSUS: the render loop does reach the graphics layer

Run of 2026-09-07 night, build `Sep  7 2026 23:37:58`.

```
GX2CENSUS total=133069 sites=24
```

**133,069 GX2 calls.** The render loop is not spinning in the scene graph -- it
reaches the graphics layer and drives it hard. The heap stays healthy alongside
it: `SPANWATCH n=3`, all three pools whole, no drops, and the archive work list
still drained at `n=0`.

Resolving the recorded call sites against the retail symbol table:

```
x13  igTexture::create+0x184
x32  igPlatformVisualContext::setTextureMaxLod+0xb8
x4   igPlatformVisualContext::initTexture+0x60
x1   igPlatformVisualContext::open+0x50
x2   igVideoFormat::ctor+0x88
x1   igCafeSystemMemory::activate+0x80
```

Display bring-up and texture creation -- exactly the shape of an engine
initialising its renderer.

## The probe was badly designed, and this is what that cost

Those sites account for about **110 calls out of 133,069**. The census keyed on
call-site `lr` into a 24-slot table that stops recording once full, so it filled
with the first few sites encountered and then dropped every hot one. The total
is trustworthy because it is a plain counter; the breakdown is nearly worthless.

This is the same mistake as the flooded rings earlier in the week -- recording
on arrival order instead of on something bounded and meaningful -- and it is
worth naming again because it survived a redesign that was supposed to have
learned the lesson.

## Fixed by keying on the function, not the call site

Build `Sep  8 2026 07:31:55`, md5 `695fe232475f268c981631af3dcd9160`, is on the
Switch.

Each of the 140 `ppc_import_gx2_*` shims now passes its own index rather than
`ctx->lr`, so the table is bounded by the number of GX2 functions and nothing
can be dropped. `cafeos_gx2_names.h` is generated from the same source, so the
report prints **names** instead of addresses, and the reporter selection-sorts
to show the busiest fourteen rather than whichever appeared first.

That turns the next run's output into a directly actionable list: the GX2 entry
points this game leans on hardest, in order, which is the order they need to
work in for anything to appear on screen.

## Where this sits

Nothing is drawn yet. But the sequence is now understood end to end: the engine
boots, drains its first archive, builds a scene, and issues six-figure numbers
of graphics calls. What remains is that those calls go to shims rather than to
real hardware, and behind that sits the known wall -- GX2 shaders are compiled
AMD R600 machine code while deko3d expects offline-compiled Nvidia Maxwell
binaries.
