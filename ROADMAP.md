# jouster roadmap

What has to work, roughly in the order it has to work. Percentages are avoided
deliberately — the useful question is which wall is next, not how far along a
number says we are.

Current position: the engine boots, runs a render loop, and draws nothing
because its first archive stops loading. See `docs/boot-chain.md` for the
measured detail.

## 1. Finish loading the boot archive — in progress

The config file is read and its values never reach the registry, so the game
asks for a level that does not ship. Immediate work:

- [ ] resolve why `igXmlNode::merge` never runs despite `igXmlDocument::read`
      returning success
- [ ] confirm `startLevel` becomes `Title` and `level/Title.bld` opens
- [ ] all 35 blocks of `permanent/bootstrap.bld` decompress
- [ ] the archive stays a registered storage device and is pumped every frame

The same failure suppresses `vramBSize`, `jobqueueBatchDataHeapSize` and every
other configured value, so fixing it once removes several unrelated-looking
symptoms.

## 2. Actually load a level

Untested territory — nothing past the bootstrap archive has ever loaded.

- [ ] `level/Title.bld` opens, decompresses and its igz objects instantiate
- [ ] `.arc` companions load alongside `.bld`
- [ ] the scene graph gains real content
- [ ] draw calls appear at all (`GX2DrawEx` / `GX2DrawIndexedEx` currently zero)

Expect this to surface object-construction and metafield bugs that the
bootstrap archive is too small to expose.

## 3. Graphics: the R600 → Maxwell wall

The largest single piece of work in the project, and now the next one rather
than a distant one. The engine already issues a complete pipeline —
viewport, scissor, display lists, shader binds, samplers, depth-stencil — into
shims.

- [ ] decide the target: deko3d, or Vulkan through a translation layer
- [ ] surface and texture formats: `GX2CalcSurfaceSizeAndAlignment`,
      `GX2InitColorBufferRegs`, tiling/swizzle modes
- [ ] **shader translation.** GX2 ships compiled AMD R600 machine code; deko3d
      wants offline-compiled Nvidia Maxwell binaries. Options, none cheap:
      decompile R600 to an IR and recompile; pattern-match the game's finite
      shader set and hand-author replacements; or interpret.
- [ ] `GX2SetContextState` → pipeline state objects
- [ ] display lists → command buffers
- [ ] present path: `GX2SwapScanBuffers` already runs 1,291 times a session

The shader set is finite and shipped on the disc, which makes the
pattern-match route more plausible here than it would be for a general
emulator.

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
- **Cemu as oracle.** `tools/cemu-pools.py` reads retail state through
  `/proc/<pid>/mem` at full JIT speed. When a structure looks wrong, check what
  the real game has there before theorising.

## Deliberately out of scope

- PowerPC emulation at runtime — this is static recompilation
- shipping any game code or asset
- supporting dumps the user does not own
