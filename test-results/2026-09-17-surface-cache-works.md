# The surface cache works. The clear is the last unknown.

Build `Sep 16 2026 21:11:32`.

    OK  build=Sep_16_2026_21-11-32 frames=14400 draws=228 modules=7

Every prediction from the previous note held, exactly:

    SURFPOOL  live=4 hit=382 miss=4 evicted=0
    SETCB     kept=382 rebuilt=4        (was kept=1 rebuilt=271)
    SETCBSEQ  clamped=0                 (was clamped=106)
    DRAWPATH  tried=222 drawn=222 || every refusal counter 0
    COPYPATH  ok=156 rejected: tile=0 other=0
    RETIRE    deferred=1225 drained=1217 peak=16 leaked=0
    [DKDEBUG] none

Four surfaces, four misses -- one per surface, on its first sight -- and 382
hits after that. Nothing evicted. The sequence shows it directly: the first
appearance of each address is a REBUILT, every later appearance is `kept`.

    [0x25848600 1280x720 REBUILT][0x25f51600 854x480 REBUILT]
    [0x25848600 1280x720 kept][0x25848600 1280x720 kept]
    [0x1948100 1024x576 REBUILT][0x1dc9100 1024x576 REBUILT]
    [0x25848600 1280x720 kept][0x25f51600 854x480 kept]
    [0x25848600 1280x720 kept][0x1948100 1024x576 kept][0x1dc9100 1024x576 kept]

`clamped=0` confirms the claim it was left in to check: with the right image
looked up per surface, every present rectangle fits by construction.

Draws 160 -> 228, and still not one refusal or validation error.

## What is left

FRAMEORD is unchanged:

    setcb setcb clear setcb DRAW setcb DRAW setcb clear copy copy swap

A clear still lands after the draws and before the present. Whether that
destroys the frame now depends on something the probe does not record: which
surface it clears. Each of the four keeps its own image, so a clear on the
drawn surface wipes the work, and a clear on a different surface is routine
double-buffer housekeeping and harmless.

The sequence is equally consistent with both. This project has already paid an
eight-build bisection for reasoning past that kind of gap, so it is being
measured instead.

## What is being measured next

Build `Sep 16 2026 21:28:15`.

Every FRAMEORD event now carries the surface-cache entry it touched, as `#n`:

  * `clear#2` after `DRAW#2` -- the clear wipes the drawn buffer. That is the
    bug, and the fix is at the clear.
  * `clear#0` after `DRAW#2` -- a different buffer. The draws survive and the
    problem is further along, most likely in what the composite draws read.
  * `copy#n` -- the surface actually presented. It either matches where the
    draws went, or it names what is being shown instead of them.

`#n` is the cache entry, and SURFPOOL plus SETCBSEQ give the address and size
behind each one.
