# Removing a divide per pixel made bash.mov visibly smoother

Build `Sep 15 2026 23:46:49`, confirmed on hardware by the owner: the movie
plays smoother than the builds before it.

## What was removed

`arkchemy_video_convert` scaled and colour-converted the decoded YUV planes on
the CPU, and its inner loop contained:

```c
uint32_t sx = (uint32_t)((uint64_t)dx * vw / dst_w);
```

A 64-bit integer divide, per pixel. At 1280x720 that is **921,600 divides per
frame**. A 64-bit divide is tens of cycles on Cortex-A57 and does not pipeline,
so it dominated a loop whose three guest reads are each a mask and a load --
`ppc_load_u8` is `ctx->shared->mem[addr & (PPC_MEM_SIZE - 1)]`, inlined.

`sx` depends only on `dx`. It is identical for every row and every frame until
the video's dimensions change, so it is now a table built once per video and
read back with one load.

Two smaller things went with it:

* the `memset` of the whole 1280x720x4 staging buffer -- **3.7MB per frame** --
  when only the letterbox bars stay black and everything inside the picture is
  overwritten by the loop regardless. Cleared once instead.
* the luma and chroma row bases, recomputed per pixel including a shift of a
  value constant across the row. Hoisted.

## How this was and was not measured

**Not** by frames presented. That reads 526 in every run on record, before and
after, because it is fixed by the file: the decoder plays every frame whatever
the frame rate. Quoting it as evidence would have been wrong, and it was
briefly tempting.

The confirmation is observational -- the owner reported the drop during the
movie, and reported it gone on this build. That is the right kind of evidence
for this particular claim, since "the video stutters" is a perceptual symptom
and the fix is arithmetic removal that cannot change the output. The pixels are
identical either way; only the time to produce them changed.

An instrumented figure would still be worth having if the video path is ever
touched again: a microsecond count around the convert loop, printed per frame
for the first few frames, would turn "smoother" into a number.

## What this does not tell us

Nothing about the render path. The movie is drawn by `arkchemy_video_present`,
which uploads its own staging buffer straight to the swapchain and never goes
near the engine's draws. The two share a frame boundary and nothing else.
