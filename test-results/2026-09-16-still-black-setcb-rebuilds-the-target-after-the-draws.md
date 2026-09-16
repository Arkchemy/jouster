# Still black: GX2SetColorBuffer rebuilds the target after the draws

Build `Sep 15 2026 23:46:49`, reported on hardware: splash screen and
`bash.mov` play as always, and **nothing else appears**. No geometry, no flat
colour, no flicker. Same as every build before it.

So binding the game's colour buffer as the render target was necessary and not
sufficient. Recording the negative result, and the lead it points at.

## What the run says

```
DRAWPATH tried=547 drawn=547 || every rejection counter 0
SHADERMOD distinct=7 loaded=7      GX2CENSUS 31,969 / 72
DKDEBUG (none)                     exiting after 14400 main frames
```

Clean, and the tally went to 13 ok / 5 failed. Nothing regressed. The fix is
not wrong, it is incomplete.

## The lead

`FRAMEORD` is unchanged, which is expected -- it records which GX2 calls happen
in what order, and the fix changed what they target, not the sequence:

```
clear  DRAW DRAW  setcb  clear  copy copy  swap
```

Read that again with the new target binding in mind. `setcb` is
`GX2SetColorBuffer`, and that function does not merely *select* a buffer. It:

1. destroys `color_target_mem_block[target]` if one is bound,
2. creates a fresh memory block and image,
3. **uploads the guest surface into it row by row**, and
4. (since 2026-09-15) binds it as the render target.

Every one of those steps runs *after* the two draws in this frame. So the image
the draws landed in is destroyed and replaced with an upload of guest memory --
which holds no drawn pixels, because the recompiled code does not rasterise.
Then the second `clear` clears whatever survived. Then the copy presents it.

The frame is still being thrown away. It is simply being thrown away by
`setcb` and the trailing `clear` now, rather than by the copy.

## Why this was not visible before

Before the target change, `setcb` rebuilding its image was harmless: draws went
to the swapchain, so nothing of value lived in the colour target and rebuilding
it destroyed nothing. Making it the render target is what turned a wasteful
no-op into a destructive one.

That is worth stating plainly because it is the second time on this problem
that a change has moved the point of destruction rather than removing it.

## What to measure next, in order

1. **Does `setcb` mid-frame actually re-upload, or is it idempotent for an
   unchanged surface?** If the guest passes the same surface descriptor every
   frame, step 1-3 could be skipped entirely when the address, dimensions and
   format are unchanged -- keeping the existing image and its contents. Cheap
   to check, and cheap to fix if true.
2. **Is anything in `color_target_image[0]` after the draws?** A GPU-side
   readback of a few pixels between the last draw and the copy settles whether
   the draws produce colour at all, independently of what happens to it
   afterwards. This is the measurement that separates "drawn then destroyed"
   from "never drawn anything visible" -- and those need completely different
   fixes.
3. Only then, the trailing `clear`.

Do **not** skip to reasoning about transforms, viewports or geometry. Those are
the explanations that feel plausible and have no evidence behind them yet, and
this problem has already cost one eight-build bisection built on exactly that.
