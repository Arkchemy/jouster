# The game is not stalled. It runs at 0.4 fps.

Build `Sep 17 2026 16:53:34`.

    INPUT   vpadreads=90 nosample=0 held_any=13 a_seen=13 kpadreads=0
    GPUCNT  frames=91
    PEEK    frames=91
    SCANTGT [1 x90] [4 x90]

Both readings the probe was written to separate turned out to be the wrong
question.

The game does read the pad -- 90 times, none refused, with A held on 13 of
them. So it is handed the synthetic press and it is not ignoring input.

But 90 reads. Against 91 presented frames, 91 PEEK samples and 90 TV scan
copies, over 14,400 host frames.

**VPADRead is called exactly once per frame the game presents.** Those 91 are
the game's own frames, and 91 frames in four minutes is **0.38 fps** -- one
frame every 2.6 seconds.

Nothing is waiting. Nothing is stuck. The boot sequence is running correctly
and running about 160 times slower than it should, and three archives with no
level is simply how far a correct boot gets in 91 frames.

That also retires the question the last three runs were built around. There is
no gate holding the level back, no button being missed, no loading failure. The
engine has not reached the point of asking yet.

## What this changes

Everything measured since 2026-09-16 still holds and none of it was wasted --
the graphics path had five genuine bugs in it and they are fixed. But the black
screen was never going to resolve on that path, and neither was the loading
one. The remaining problem is throughput.

## What is being measured next

Build `Sep 17 2026 17:08:30`.

`TIMING frames= frame_avg= | copyup= texup= setcbup= waitidle=`, in
milliseconds, instrumented around the actual work rather than inferred.

The suspects are this file's own per-pixel loops, which walk guest memory one
byte at a time through `ppc_load_u8`:

  * the present upload, 1280x720x4 per presented frame
  * the texture upload, 1024x576x4, and `TEXSRC` says 180 a run still take the
    guest path
  * `GX2SetColorBuffer`'s, now mostly skipped -- `SURFPOOL hit=448 miss=4`
  * plus the unconditional `dkQueueWaitIdle` after every submit

Those are candidates and nothing more. This project has been wrong every time
it reasoned about its own timing from call counts -- "526 frames presented" was
identical before and after a real fix to the video path -- so `frame_avg` is
measured present-to-present and every other figure is a share of it.

If the four together do not account for the frame, the cost is in the
recompiled code itself and that is a much larger piece of work.
