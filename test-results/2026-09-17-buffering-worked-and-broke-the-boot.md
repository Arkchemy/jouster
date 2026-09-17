# Buffering worked, and broke the boot

Build `Sep 17 2026 18:15:43`.

    LOGCOST lines=27293 log=1534ms flush=1243ms

Against 114,060ms and 109,547ms the run before. **88x.** The diagnosis was
right and the fix does what it was meant to.

And the run is much worse.

                        18:15:43   17:33:11
    GPUCNT frames              1        103
    INPUT vpadreads            0         90
    SHADERMOD distinct         2          7
    DRAWPATH tried             0        305
    STREAMLOAD calls           0          3
    INFLATE lzma n             0        139
    ARCHNAME n                 0          4
    RSALLOC calls              0         15
    guest calls        2,685,524  3,875,018

One presented frame. No archive opened. Nothing decompressed. It never reached
`igMemoryPool::mallocAligned` at all, where the run before called it fifteen
times.

The guest was not blocked. Its call counter rose steadily to the end -- 13,000
per sixty host frames, all the way through -- so it ran the whole time and ran
somewhere else. `last_pc` sat at `0x214cf74` against `0x25835f4` before. It took
a different path very early in boot and stayed on it.

## The suspect

`PPC_MEM_SIZE` is one gigabyte, and the guest arena is a static array of it --
`uint8_t mem[PPC_MEM_SIZE]` inside `PpcSharedMemory`. The NRO's BSS is already
a gigabyte before anything else is in it.

This change added `static char g_log_buf[256 * 1024]` to that. A quarter
megabyte is nothing against a gigabyte until the gigabyte is the thing sitting
at the edge of what the loader can satisfy, and then it is the difference.

That is the only thing the build added that is not logic. It is a suspect, not
a conclusion.

## What is being tested next

Build `Sep 17 2026 18:25:22`. One variable: the same buffering, the same flush
interval, an eighth of the memory.

At roughly 350 bytes a line a 32KB buffer fills every ~90 lines, so stdio
flushes about 300 times a run rather than 27,854 -- still around a 90x
reduction, and 1,243ms says the remaining cost is small enough not to matter.

  * boot comes back -- the BSS was the cause. The buffer can then be tuned
    upward against real numbers, and the 60% finding stands with a working
    fix behind it.
  * boot stays broken -- buffering itself is implicated, the 256KB was a red
    herring, and the next question is what about batched writes to this SD
    path changes the guest's behaviour. That would be a strange result and
    worth more than a quick revert.

Either way the measurement that produced this stands: the per-line flush was
60% of every run, and every timing figure this project has recorded includes
it.
