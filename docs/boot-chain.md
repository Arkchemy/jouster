# The boot chain, as measured

Where the recompiled game actually gets to, and what stops it. Every figure
here came off hardware; nothing is estimated. Individual runs are written up in
`test-results/`, newest first — this is the standing summary.

## Root cause found (2026-09-09)

The chain below was measured correctly link by link, and **not one link was
the bug**. The cause sat above all of them, in the recompiler:
`igArkCore::init` gates the entire configuration load on a single `lbz` of
`igRegistry::_autoLoad`, and eleven D-form load/store handlers in
`codegen.cpp` were adding a relocation placeholder on top of an
already-relocated address. That byte was read 8,528 bytes short of the flag,
came back zero, and the registry load was skipped whole.

Fixed in conquertron `188bf6f`; 423 instructions in this binary were
affected, all narrow accesses to relocated globals. Full write-up:
`conquertron/findings/2026-09-09-lo-reloc-fold-missing-on-narrow-loads.md`.

The `XMLWHO` run that pointed at it reported `igRegistry::read calls=0` with
exactly one caller of `igXmlDocument::read` — and that caller turned out to
be the sibling `read(const char*)` overload, not a real client. The rest was
static: walking the retail RPX's call graph, `igArkCore::init` is the *only*
caller of `igRegistry::read`, and `__sti___22_tfbCafeApplication_cpp` is the
only writer of `_registryPath`.

**Confirmed on hardware, same day.** `XMLWHO igRegistry::read calls=1`, and
the caller of `igXmlDocument::read` is now `lr=0x21c3a5c` — inside
`igRegistry::read` itself, which is a real client rather than the sibling
overload the previous run reported. The registry loads.

## Second wall, immediately behind it (2026-09-09)

Loading the registry runs a path that had never executed, and it deadlocked
on the first file read:

```
igArkCore::init -> igRegistry::read -> igPhysicalStorageDevice::update
  FSReadFileWithPosAsync   -- shim reads alchemy.xml, QUEUES the completion
igCafeSignal::wait -> b OSWaitEvent   -- parks forever
```

```
fs: ard=1/542  fsz=542  head="<root>\n\t<Core\n\t\t"   <- the file is IN the buffer
asyncq: q=1 done=0 pend=1     cb: ok=0     evt: sig=0
threads: created=0 started=0
guest calls frozen at 159,416, last_pc = igCafeSignal::wait
```

`OSWaitEventWithTimeout` pumped queued FS completions before parking;
`OSWaitEvent` did not. That only worked because two workers normally sit in
the timeout variant forever and one of them always ran the pump. The registry
read happens *before any guest thread is created*, so there was nobody left to
drain a queue one entry deep.

Fixed in conquertron `104a16d`. Write-up:
`conquertron/findings/2026-09-09-oswaitevent-never-pumped-fs-completions.md`.

**Also found:** `codegen.cpp` had not compiled since `9ca5e40` (2026-09-03) —
`ppc.id` where `cs_ppc` has no such member. `regenerate.sh` had been silently
using a stale `build/recomp`, so the per-instruction memory barriers that
commit added have **never been in a generated build**. Fixed; the current tree
still predates them, deliberately, so that the OSWaitEvent fix is the only
variable in the next run.

## State before the fix (2026-09-08)

The engine boots, loads and decompresses part of its first archive, builds a
scene, and runs a **complete render pipeline** — 134,606 GX2 calls across 53
distinct entry points, presenting 1,341 frames without crashing.

It draws nothing, because the scene is empty, because the archive stopped
loading. That is a **file-loading problem, not a graphics one.**

```
globals_init=1  static_init=1 (113/114)  game_started=1
guest calls ~8.8M   files opened 5   frames 25,200   clean exit, no exception
```

## The chain, link by link

Each of these was measured separately, and each explains the next.

```
igXmlDocument::read(alchemy.xml) succeeds (ret=0, no parse errors)
  -> but igXmlNode::merge is never called                     [XMLMERGE n=0]
  -> the registry keeps its compiled-in defaults
  -> startLevel stays "test"                                  [XMLVAL]
     and vramBSize stays a default rather than 419430400
  -> streamContext::load closes bootstrap.bld to open level/test.bld
  -> level/test.bld does not exist on the disc                [FS: NOT FOUND]
  -> the open fails and never registers as a storage device   [DEVLOG ADD/REM]
  -> igFileContext::update walks 8 devices, none of them the archive
  -> igArchive::update runs 8 times against the context's 1,341   [ARCHDRIVE]
  -> startNewTasks starts 1 block read of 35                  [ARCHPUMP]
  -> 131,072 of 198,695 bytes read, 1 block decompressed
  -> the scene has no content
  -> 1,341 frames presented, zero draws                       [GX2DRAW]
```

The open question sits at the top: the parse **succeeds** and the merge still
never runs, which `igRegistry::read` should not permit —

```
21c3aa0: bl  0x21e287c   ; igXmlDocument::read(igFile*)
21c3aa4: or. r31, r3, r3
21c3aa8: bne 0x21c3acc   ; NON-zero -> skip the merge
21c3ac8: bl  0x21e07f4   ; merge(registryRoot, fileRoot, 1)
```

With `ret=0` that branch is not taken. Either `igRegistry::read` never runs, or
the successful read came from a different caller. That is what `XMLWHO`
measures next.

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

**Not a race.** `ALLOCRACE peak=1 overlaps=0` — only ever one thread inside the
allocator.

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

Once the archive loads, the next wall is known and large: GX2 shaders are
compiled AMD R600 machine code, and deko3d wants offline-compiled Nvidia
Maxwell binaries. The render pipeline reaching the hardware at all means that
wall is now the *next* problem rather than a distant one — see
`ROADMAP.md`.
