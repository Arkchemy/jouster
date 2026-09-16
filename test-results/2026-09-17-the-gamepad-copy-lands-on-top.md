# The clear is innocent. The GamePad copy lands on top of the frame.

Build `Sep 16 2026 21:28:15`.

    OK  build=Sep_16_2026_21-28-15 frames=14400 draws=247 modules=7

FRAMEORD now tags every event with the surface-cache entry it touched:

    setcb#0 setcb#1 clear#1 setcb#0 clear#0 copy#0 copy#1 swap
  | copy#0 copy#1 swap
  | setcb#0 setcb#2 clear#2 setcb#3 DRAW#3 setcb#0 DRAW#0 setcb#1 clear#1
    copy#0 copy#1 swap#0

    #0  0x25848600  1280x720   TV scan buffer
    #1  0x25f51600   854x480   GamePad scan buffer
    #2  0x1948100   1024x576   offscreen target A
    #3  0x1dc9100   1024x576   offscreen target B

## The clear was not the problem

The question was whether `clear` after the draws lands on the surface those
draws wrote. It does not. The draws go into `#3` and `#0`; the clear that
follows them is `clear#1`, the GamePad buffer. That is ordinary
double-buffer housekeeping and it destroys nothing.

Read straight through, the drawing frame is correct:

    clear#2          clear offscreen A
    DRAW#3           draw the scene into offscreen B
    DRAW#0           composite into the TV scan buffer
    clear#1          clear the GamePad buffer
    copy#0           present the TV scan buffer   <- the composited frame
    copy#1           present the GamePad buffer   <- lands on top of it
    swap#0

The engine's pipeline is render-to-texture into a 1024x576 target, composited
into the 1280x720 TV scan buffer, and `copy#0` presents exactly that. At that
point in the frame the picture is intact.

## What overwrites it

`GX2CopyColorBufferToScanBuffer(colorBuffer, scanTarget)` takes the target as
its second argument, and this shim ignores it. Its own header comment says so:

    r4=scanTarget (`GX2_SCAN_TARGET_TV`/`_DRC` -- ignored here, same real,
    already-documented simplification as GX2ClearColor's own: this runtime
    has one real display target, the Switch's own screen, not a separate
    TV/DRC pair).

Both copies therefore blit into the same acquired swapchain image, and the
second one wins. `copy#1`'s source is `#1`, which holds nothing but the colour
`clear#1` put there one call earlier. So the composited frame is presented,
and then 854x480 of cleared buffer is laid over its top-left corner.

The simplification was sound while it lasted: with `COPYPATH ok=0` no copy
ever ran, and with one shared slot every copy read the wrong surface anyway.
It only became the live defect once the frame reaching `copy#0` was correct.

## Fix

Build `Sep 16 2026 22:11:34`. Copies whose scanTarget is not the TV are
dropped. On real hardware that copy goes to the GamePad's own screen; there
is one screen here, so the choice is to drop it or to draw it over the frame.

`SCANTGT` records the raw values with per-target counts rather than trusting
the gate. wut's `gx2/enum.h` gives `GX2_SCAN_TARGET_TV = 1` and
`GX2_SCAN_TARGET_DRC0 = 4`, and the gate depends on that, so if TV is not 1
the log says so directly instead of leaving a black screen to be re-diagnosed.

## What to read next

  * `SCANTGT [1 xN] [4 xM] skipped=M` -- two values, and `skipped` equal to
    the count against 4. Two values with `skipped=0` means the gate is
    matching the wrong one.
  * `COPYPATH ok=` should roughly halve. That is the point, not a regression.
  * `FRAMEORD` should show `copy#0 swap` with no `copy#1` after it.
  * And whether anything is actually on screen. Every measured step from
    allocation to present is now accounted for, so if it is still black the
    next question is what the draws themselves produce -- shader translation,
    vertex data, transforms -- which nothing here has tested yet.
