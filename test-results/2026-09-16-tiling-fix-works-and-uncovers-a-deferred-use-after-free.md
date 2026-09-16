# The tiling fix works, and it uncovered a use-after-free that was hiding behind it

Build `Sep 16 2026 20:13:14`, run at 20:19.

## What the fix did

`GX2CalcSurfaceSizeAndAlignment` was widened so `GX2_TILE_MODE_DEFAULT` on a
2D surface resolves to `TM_LINEAR_ALIGNED` instead of falling into the
documented gap that left every guest field untouched.

All three predictions held, against an unbroken run of zeros before it:

    COPYSURF 2 distinct surfaces handed to the present path:
      [1280x720 mips=1 fmt=0x1a tile=1 pitch=1280 addr=0x25848600]
      [854x480  mips=1 fmt=0x1a tile=1 pitch=896  addr=0x25f51600]
    COPYPATH ok=2 rejected: tile=0 other=0
    SETCB   kept=0 rebuilt=3   rejected: dim=0 tile=0 mip=0 fmt=0x0 zero=0

Both colour buffers have memory for the first time. `COPYPATH ok` is non-zero
for the first time in any build: the engine's own frames reached the scan
buffer. Every rejection counter is zero.

That confirms the diagnosis in
`2026-09-16-still-black-setcb-rebuilds-the-target-after-the-draws.md`: the
blockage was allocation, at the very first step, not tiling support or
transforms or geometry.

## What it uncovered

The run then stopped dead.

    main frame 7380/14400 ... calls=1711253  last_pc=0x21566e4
    main frame 7440/14400 ... calls=1711253  last_pc=0x21566e4
    main frame 7500/14400 ... calls=1711253  last_pc=0x21566e4
    main frame 7560/14400 ... calls=1711253  last_pc=0x21566e4

The host loop kept running; the guest made no progress for 180 frames. The
debug deko3d callback says why:

    [DKDEBUG]   dkCmdBufBarrier: Queue (0) entered error state
    [DKDEBUG]   dkCmdBufBarrier:   GPU page fault (info 0x00001842)
    [DKDEBUG]   dkCmdBufBarrier:   Address: 0x0502280000
    [DKDEBUG]   dkCmdBufBarrier:   Access type: Read
    [DKDEBUG] deko3d raised DkResult_Fail (1) in 'dkQueueSubmitCommands':
              attempt to submit commands to a queue in error state

One fault, and the queue is dead for the rest of the process. Every later
submit fails, so the guest thread never advances again.

## Cause

Every `dkCmdBuf*` call in the shim RECORDS. Nothing the GPU reads is touched
until `dkQueueSubmitCommands` at swap. The frame order shows what happens in
between:

    FRAMEORD n=8 frames=1 : setcb setcb clear setcb clear copy copy swap

Two copies and three colour-buffer rebuilds, all ahead of the single submit.

  * `GX2CopyColorBufferToScanBuffer` destroyed `scan_copy_temp_mem_block` and
    made a new one on entry. The second copy of the frame therefore freed the
    memory the first copy's already-recorded blit reads.
  * `GX2SetColorBuffer` destroyed `color_target_mem_block[target]` on rebuild.
    The third setcb freed the memory the first clear was recorded against.

Both are deferred use-after-frees. The fault surfaces at whatever sync point
the queue notices, naming neither the call that freed the memory nor the frame
it happened in -- here, a `dkCmdBufBarrier`.

The struct comment on `scan_copy_temp_mem_block` had already identified the
hazard and said the block is "replaced (not freed right after recording) on
the next real call". That is correct only if the next call comes after the
submit. With two copies per frame it does not.

## Why it never fired before

It was in the code the whole time and was unreachable. `COPYPATH ok=0` in
every previous build means no blit was ever recorded against these blocks, so
freeing them cost nothing. Fixing the allocation made five destroy sites live
at once.

## Fix

Build `Sep 16 2026 20:33:02`.

A retirement list on `ArkchemyGx2State`. `arkchemy_gx2_retire_memblock()`
holds a replaced block; `arkchemy_gx2_drain_retired()` destroys the held
blocks, and is called only immediately after a `dkQueueWaitIdle` that follows
a submit -- in `GX2SwapScanBuffers`, `GX2DrawDone`, `GX2WaitForVsync` and
teardown. `GX2WaitTimeStamp` deliberately does not drain: it waits without
submitting first, so commands recorded since the last submit are still
pending and their memory has to stay.

All six mid-frame destroy sites route through it: the colour target, both
depth blocks, both texture blocks, the scan-copy staging block, and the
vertex-buffer growth path in `cafeos_gx2_draw.h`.

Overflow leaks rather than destroys, and counts. A leak costs memory and
shows up in the probe; a destroy costs the queue and the whole run.

## What to read next

`RETIRE deferred=N drained=N peak=N leaked=N`.

  * `deferred - drained` no larger than `peak` is the frame in flight.
  * A larger standing gap is a real leak.
  * `leaked > 0` means `ARKCHEMY_GX2_RETIRE_MAX` (64) is smaller than what a
    frame actually recycles. That is a sizing number to raise, not a crash.

And whether any `[DKDEBUG]` line appears at all. If the queue survives, the
next question is what the presented frame contains, which `DRAWPATH tried/drawn`
answers -- it was still `tried=0 drawn=0` when the queue died at frame 7380,
so nothing here has yet shown the engine's geometry reaching the screen.
