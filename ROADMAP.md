# jouster roadmap

What has to work, roughly in the order it has to work. Percentages are avoided
deliberately — the useful question is which wall is next, not how far along a
number says we are.

Current position: the engine boots, loads its boot archive and three more
behind it, translates the shaders the game binds into deko3d modules, and
issues real draw calls. The next wall is state and textures, not shaders.

One caveat governs everything below: **the rig fails about two runs in five,
on an unchanged binary.** See
`test-results/2026-09-14-the-same-build-fails-two-runs-in-five.md`. Nothing
here should be concluded from a single run, and several things below are
recorded as done on evidence that predates that discovery.

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
