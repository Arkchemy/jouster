# The boot chain, as measured

Where the recompiled game actually gets to, and what stops it. Every figure
here came off hardware; nothing is estimated. Individual runs are written up in
`test-results/`, newest first — this is the standing summary.

## Read this first (2026-09-14)

**The rig fails about two runs in five, on an unchanged binary.** Seven loop
cycles completed and five failed over 2026-09-13/14 with nothing written to the
card between them. Write-up:
`test-results/2026-09-14-the-same-build-fails-two-runs-in-five.md`.

Everything below was measured before that was known, which means any figure
here resting on one run rests on a coin toss. The numbers quoted are from
*healthy* runs and are reproducible across several; the conclusions about what
a particular build does or does not do are the ones to distrust.

The failure is bimodal and has no middle: a run either reaches ~28,000 GX2
calls with ~500 draws, or 826 calls with zero. There is no partial outcome on
record.

## Where a healthy run now gets to (2026-09-14)

Past every wall this document was originally written about.

```
registry loads         startLevel="Title"  vramBSize=419430400
archives opened (4)    permanent/bootstrap.bld -> item/legal.bld
                       -> permanent/global.arc -> permanent/global.bld
shader modules         SHADERMOD distinct=7 loaded=7 missing=0 refused=0
draws                  DRAWPATH tried=489 drawn=489, no rejections
GX2 calls              ~28,000 across 72 distinct entry points
exit                   clean, 14,400 main frames
```

Nothing is on screen yet. Draws are being issued and accepted; what stands
between that and a picture is state and texture setup, not shaders.

## What a failed run looks like

```
GX2CENSUS total=826 distinct=56    SHADERMOD distinct=2    DRAWPATH tried=0
ARCHNAME  n=1 [permanent/bootstrap.bld]
fs: open=3 ard=3/199237   rdq: sz=67623 fsz=198695
asyncq: q=3 done=3 drop=0 pend=0   threads: created=13   (healthy: 15)
```

Two things worth reading carefully. It is **not hung** — the main frame counter
is still advancing where the log stops (`12360/14400`), so the render loop
runs and the engine is what stalls. And **no I/O is outstanding**: every async
read issued has completed and been delivered, and the request it sits on is the
67,623-byte tail of the boot archive (198,695 − 131,072). So this is not the
2026-09-09 undrained-completion bug returning. The read completed and nothing
consumed it. Unexplained.

## How it got here (2026-09-09, both fixed)

**The registry never loaded.** `igArkCore::init` gates the whole configuration
load on a single `lbz` of `igRegistry::_autoLoad`, and eleven D-form
load/store handlers in `codegen.cpp` added a relocation placeholder on top of
an already-relocated address. That byte was read 8,528 bytes short of the flag,
came back zero, and the load was skipped whole. Fixed in conquertron
`188bf6f`; 423 instructions in this binary were affected. Write-up:
`conquertron/findings/2026-09-09-lo-reloc-fold-missing-on-narrow-loads.md`.

**Then the first read deadlocked.** `OSWaitEventWithTimeout` pumped queued FS
completions before parking; `OSWaitEvent` did not. That only worked because two
workers normally sit in the timeout variant forever and one always ran the
pump — but the registry read happens *before any guest thread is created*, so
nobody was left to drain a queue one entry deep. Fixed in conquertron
`104a16d`. Write-up:
`conquertron/findings/2026-09-09-oswaitevent-never-pumped-fs-completions.md`.

**Also found then:** `codegen.cpp` had not compiled since `9ca5e40`
(2026-09-03) — `ppc.id` where `cs_ppc` has no such member — and
`regenerate.sh` had been silently using a stale `build/recomp`, so that
commit's per-instruction memory barriers had never been in a generated build.

## What is already fixed, and should not be re-investigated

**The boot stall (fixed 2026-09-07, verified).** A new guest thread's `r1` was
set to the raw stack argument, with no PowerPC EABI linkage area reserved above
it. `jqWorkerThread`'s prologue (`mflr r0; stwu r1,-0x10(r1); stw r0,0x14(r1)`)
therefore wrote a zero four bytes past the end of its 64 KB stack, onto the size
word of a free TLSF block. The allocator then skipped an alignment split,
returned an unaligned pointer into a zero-size block, and 3.79 MB of a 5 MB
arena fell out of the block chain. Fixed in
`conquertron/include/cafeos_coreinit_thread.h` by reserving 16 bytes and
terminating the back chain.

Everything below is downstream of that and is now healthy:

- three heap pools, all reaching their sentinels, no drops across 2,748 walks
- LZMA decodes correctly: `err=0`, input and output lengths both as expected
- the archive's first work item drains

**Not the allocator.** The recompiled TLSF was cleared twice over: a captured
804-call trace replays correctly on x86-64 *and* on ARM64 on the Switch itself.

**Not the block manager.** `allocEarly=2 allocFinal=0` — allocation succeeded
both times through an exit an earlier probe was not watching. 37 of 48 observed
blocks were free.

**Not a race — but this one is now worth reopening.** `ALLOCRACE peak=1
overlaps=0` says only ever one thread was inside the allocator. The counter is
not atomic and can only undercount, so `peak==1` was always weak evidence
rather than proof. It was recorded when the boot was believed to be
deterministic; it is not, and a ~40% intermittent failure is exactly the shape
of thing this probe is too blunt to see.

## Reference points

Retail SSA under Cemu, read through `/proc/<pid>/mem`, is the oracle. Its
identically sized 5 MB heap pool accounts for **99.8%** of its arena — which is
what established that abandoning part of an arena is not normal engine
behaviour. See `tools/cemu-pools.py`.

Useful constants, all verified rather than assumed:

| Thing | Value | How it was established |
| --- | --- | --- |
| TLSF control size | `0xc70` | `tlsf_walk_heap` at `0x21f021c`; confirmed against retail |
| Block header | `+0 prev_phys, +4 size\|flags, +8/+0xc free links` | same |
| `size = word & ~3`, bit 0 = free | | same |
| Next block | `block + 4 + size` | same; walk self-checks to the byte |
| Pool accounting | `+0x2c blocks, +0x34 userAllocated` | `updateStatistics` at `0x2173648` |
| `getIsActive` | `+0x20` byte (virtual) / `+0x2c` word (physical) | two different implementations |

## Beyond this

The archive wall and the shader wall are both behind us. GX2 ships compiled AMD
R600 machine code and deko3d wants Nvidia Maxwell binaries, and as of
2026-09-12 all seven programs the engine binds are translated, compiled with
`uam`, and loading on hardware.

What is left before a picture: surface and texture formats, fetch shaders to
deko3d attribute state, context state to pipeline objects. See `ROADMAP.md`
section 3, and `docs/graphics-plan.md` for the measured detail.

The open question is no longer "can the shaders be translated" but "why is
nothing visible when 489 draws are issued and accepted" — and, before either,
why two runs in five never get that far at all.
