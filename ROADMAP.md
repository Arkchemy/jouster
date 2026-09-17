# jouster roadmap

What has to work, roughly in the order it has to work. Percentages are avoided
deliberately — the useful question is which wall is next, not how far along a
number says we are.

Current position (2026-09-17): the engine boots, loads its boot archive and
three more behind it, translates every shader it binds into deko3d modules,
issues real draw calls, and those draws now reach the screen through a present
path that is correct end to end. **The next wall is throughput, not graphics
and not loading.**

The graphics chain was five separate faults stacked on each other, all found
and fixed on 2026-09-16: a tiling gap that meant colour buffers were never
allocated, a deferred use-after-free that killed the GPU queue, four surfaces
sharing one render-target slot, an ignored scan target letting the GamePad copy
overwrite the TV frame, and render targets bound as textures being overwritten
with empty guest memory. deko3d's own pipeline counters now report the geometry
rasterising and passing the depth test.

What is on screen is still black, and that is no longer a graphics question.
`DRAWSIZE quad=296 max=4` — every draw in a run is a four-vertex full-screen
composite quad, so no scene geometry is ever submitted. The loader works and
has loaded bootstrap, legal and global; no level is ever requested. The boot
sequence is correct and has not got that far, because the game runs at
**0.28 fps** — 102 game frames in a 366-second run, measured directly.

The caveat that used to govern this file is retired. It read "the rig fails
about two runs in five, on an unchanged binary", from
`test-results/2026-09-14-the-same-build-fails-two-runs-in-five.md`. The run
tally now shows **30 consecutive clean runs**, and the last failure was build
`Sep_16_2026_20-13-14` — the deferred use-after-free, fixed the same evening.
Single runs can be trusted again, which is worth stating explicitly because
several conclusions below were deliberately hedged on the old figure.

One caveat replaces it, and it is smaller: **the log cannot be buffered.**
Buffering `checkpoint()`'s stream saves 60% of every run and stops the game
booting, isolated on identical binaries with one setting changed
(`test-results/2026-09-17-buffering-isolated-two-causes-confirmed.md`). Why is
unknown. Until it is, a third of every run is instrumentation that cannot be
removed, and timing figures should be read with that in them.

## 1. Finish loading the boot archive — done

The registry loads and its values reach the engine. `startLevel` is `Title`,
`vramBSize` is 419430400 rather than a compiled-in default, and the chain that
used to stop at `permanent/bootstrap.bld` now continues past it.

- [x] `igXmlNode::merge` — moot. The root cause was upstream: eleven D-form
      load/store handlers double-relocated an already-relocated address, so
      `igArkCore::init` read `igRegistry::_autoLoad` 8,528 bytes short.
      Fixed in conquertron `188bf6f`.
- [x] `OSWaitEvent` never pumped queued FS completions, deadlocking the first
      read. Fixed in conquertron `104a16d`.
- [x] `startLevel` becomes `Title` — confirmed, `XMLVAL ["startLevel"="Title"]`
- [x] the archive registers as a storage device and is pumped

## 2. Actually load a level — in progress, and further than expected

A healthy run now opens four archives in sequence:

```
permanent/bootstrap.bld -> item/legal.bld -> permanent/global.arc
                                          -> permanent/global.bld
```

- [x] archives past the bootstrap open and decompress
- [x] `.arc` companions load alongside `.bld`
- [x] draw calls appear — `GX2DrawEx` is called ~490-590 times a run.
      (The `DRAWPATH drawn` counter double-counted until 2026-09-14, so
      figures quoted as 489/513 *draws recorded* were really 245/257; the
      `GX2DrawEx` call counts themselves are unaffected.)
- [ ] `level/Title.bld` itself
- [ ] the scene graph gains content that survives to the screen

## 3. Graphics: the R600 wall, partly climbed

Still the largest single piece of work, but no longer untouched. The shader
half is done; the state half is not.

- [x] **target chosen: deko3d.** `GX2Init`'s shim builds a real swapchain, and
      the bridge covers rasterizer, depth-stencil, colour and multisample
      state, sampler and texture descriptor pools, render targets and copies.
- [x] **shader translation.** The R700 ISA document arrived 2026-09-12.
      `blaster/r600.py` disassembles CF, TEX and ALU clauses; `r600_glsl.py`
      emits GLSL; `build-shaders.sh` compiles each with `uam` into a deko3d
      `.dksh`. All seven programs the engine binds translate, compile, and
      **load on hardware** — `SHADERMOD distinct=7 loaded=7 missing=0
      refused=0`.
- [x] **identifying a shader.** By content hash (FNV-1a 64), not pointer: the
      same shader is re-bound 57,438 times a run from allocator-chosen
      addresses, and two of the seven are both exactly 400 bytes.
- [x] **display lists — answered.** `GX2BeginDisplayListEx` and
      `GX2EndDisplayList` are both zero while `GX2CopyDisplayList` runs 42,302
      times, *and* the engine issues 8,581 immediate `GX2DrawEx` calls. It
      replays prebuilt streams and draws immediately. The immediate path is
      the one the translated shaders serve, and the one now working.
- [x] **the two assumed mappings, measured.** Attribute *N* arrives in GPR
      *N+1*, and uniform-register offsets are u32 words so constant-file index
      *n* sits at offset *4n*. Both probed on hardware (`FETCHATTR`, `UNIFREG`)
      rather than reasoned about.
- [ ] surface and texture formats: `GX2CalcSurfaceSizeAndAlignment`,
      `GX2InitColorBufferRegs`, tiling/swizzle modes
- [ ] fetch shaders → deko3d attribute state
- [ ] `GX2SetContextState` → pipeline state objects
- [ ] prebuilt display lists → command buffers
- [ ] **something visible on screen.** Draws are being issued and nothing has
      appeared yet; that gap is the next real question.

### The draw call that will not live in the shim

`dkCmdBufDraw` called from inside the GX2 shim header appeared to stop the
game booting, while the identical call from `main.c` was harmless, as was
`dkCmdBufDrawIndexed` from the shim. Eight builds narrowed it that far and no
further.

**Treat that conclusion as unproven.** It was bisected one run per build,
before the rig was known to fail ~40% of runs on an unchanged binary. The
workaround stands — `ark_draw_ex` queues the parameters and `main.c` drains
the queue — because it works, not because the diagnosis behind it is sound.
Re-running the question properly needs N cycles per build, not one.

## 3b. Throughput — the current wall

0.28 fps. Not a stall: the boot sequence is correct and simply has not had time
to reach a level. Measured shares of a 366-second run:

```
log       113,866ms   31%   cannot currently be removed (see above)
uploads    16,814ms    5%   the per-pixel ppc_load_u8 loops in cafeos_gx2.h
the rest              64%   the recompiled game
```

Deleting the log and every upload loop together takes 0.28 fps to about 0.44,
which does not reach a level. The 64% is the only part big enough to matter.

`GUESTHOT` sampled it once per host frame over a full run
(`test-results/2026-09-17-the-guest-profile-names-the-lever.md`):

```
11%  __sti___22_hkTypeInfoRegistry_cpp
 9%  Core::igMetaField::reset
 6%  Core::igMetaField::resetByValue
 6%  Core::igMetaField::construct
 5%  Core::igMetaField::commission
 2%  Core::igRefMetaField::commission
 2%  Core::igScopeLock::~igScopeLock
 1%  Core::igScopeLock::igScopeLock
 1%  Core::igObject::isOfType
 1%  Core::igBidirectionalHeapMemoryPool::contains
 1%  Core::igCafeMutex::lock
 1%  Core::igMemoryPool::updateStatistics
```

The metafield system is 28% on its own and every entry is a small,
frequently-called leaf.

- [ ] **Build at `-O2`.** The Makefile has said `-O0 ... a real, separate
      optimization pass once this actually boots` since before the first build.
      It boots. `-O0` is worst precisely where this profile is concentrated:
      nothing inlines and every local round-trips through the stack. Now
      `ARK_OPT ?= -O2`, overridable. **Unverified** — the first clean rebuild
      was still running when this was written, and the risk is real: an
      optimised build that boots less far is a miscompilation of 217
      machine-translated units, not a win. `draws` and `modules` must hold at
      ~300 and 7.
- [ ] Find out why buffering the log stops the boot. Worth 31% of every run,
      and the mechanism is likely to be a real runtime bug rather than a
      logging one.
- [ ] Replace the per-pixel upload loops. Only 5%, so worth doing after the
      two above rather than before — this was nearly done first, on the
      assumption it was the bottleneck, and it was not.
- [ ] Ask whether the metafield volume is *sane* as well as slow. 28% in object
      construction may be the engine doing normal work slowly, or it may be
      looping. Nothing has measured the absolute call count yet.

## 4. Audio

Untouched. `snd_core`/`snd_user` shims exist and are stubs. The disc's
`Item_Pet_*.arc` files carry 3–40 audio banks each, so the data side is
well understood even though nothing plays.

## 5. Input and the Portal

- [ ] VPAD → Switch controllers
- [ ] the Portal of Power is USB HID; `nsyshid` is shimmed but unexercised.
      Figure reading is a whole subsystem and is deliberately last.

## Standing engineering work

Not a phase — these run alongside everything above.

- **Codegen correctness.** Silent translation bugs have been found and fixed
  repeatedly, each producing wrong behaviour with no crash and no warning. Audit
  unusual instructions against real semantics rather than assuming: wrapping
  `rlwinm` masks, `lwzu` base update, `srawi`/`addze` carry, `slw`'s 6-bit rule.
- **Host-side testing.** `conquertron/hosttest` runs recompiled translation
  units natively. Every subsystem that does not need the OS should be testable
  there rather than on hardware.
- **Probe discipline.** See `conquertron/docs/probe-design.md`. Six distinct
  probe-design failures each cost a full hardware cycle this week.
- **Run discipline.** The rig fails about two runs in five on an unchanged
  binary, so a single run decides nothing. `arkchemy_run_tally_open` /
  `_close` in `game/source/self_update.c` write one OK or FAIL line per run to
  `run-tally.txt` on the card, keyed by build stamp, so a build's outcome can
  be read as a rate. Compare builds by rate over N cycles, never by one run.
- **Cemu as oracle.** `tools/cemu-pools.py` reads retail state through
  `/proc/<pid>/mem` at full JIT speed. When a structure looks wrong, check what
  the real game has there before theorising.

## Deliberately out of scope

- PowerPC emulation at runtime — this is static recompilation
- shipping any game code or asset
- supporting dumps the user does not own
