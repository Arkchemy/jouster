# Rebuilding the 2026-08-24/27 game-boot probes

The 526KB `game-results.log` from the 2026-08-27 08:09 build (recorded in
`test-results/2026-08-27-game-boot-null-registry-table.txt`) was produced
partly by instrumentation that is *not* in any repository. This note is
what is needed to write it again, taken from the log itself.

## What is NOT lost

Most of it is committed and works today:

- **The whole harness** — `game/source/main.c`: the checkpoint logger,
  the per-frame state dump, the `[MEM EVENT #n]` emitter, the four `w0`–`w3`
  watch slots and every `loopwatch` slot.
- **The allocation probes** — `conquertron/include/cafeos_coreinit_mem.h`:
  `DEFHEAP arena spent, falling back to ExpHeap`,
  `MEMAllocFromExpHeapEx (large alloc)`, `(out of space)`, and
  `MEMGetAllocatableSizeForExpHeapEx (query)`.

That means **the first of the two findings in that log — static
initialiser 87 exhausting the 116MB ExpHeap through ~927 unfreed 128KB
allocations — is reproducible today with no new code at all.** Build,
run, read the `sti_idx=87` lines.

## What IS lost, and how to put it back

Eight ad-hoc probes from that investigation. Each was a call into the
same `[MEM EVENT #n]` emitter, which takes four `uint32_t` carried in
fields named `requested / heap_base / heap_size / heap_used` — the label
lists what those four actually are, in order. (Confirmed rather than
assumed: `FOREIGN REALLOC`'s fourth value, 0x214AF64, is exactly the
`caller_lr` the same line prints separately.)

The log gives the exact hook site for each: `last_pc` is the recompiled
function that was executing, `caller_lr` its caller. All fire at
`sti_idx=113` (the game entry point) except where noted.

| Probe label | The four values | last_pc | caller_lr |
| --- | --- | --- | --- |
| `CTXCTOR` | ret / vtable / vtable+0x34 / bootstrap_heap_handle | 0x215c4cc | 0x217859c |
| `NULL TABLE` | dispatchTarget / thisObj / result / result+0x14 | 0x215db1c | 0x21658c8 |
| `NULL REGISTRY ENTRY` | table / index / byteoff / caller_lr | 0x215db1c | 0x21658c8 |
| `NULL OBJECT` | reallocateFieldMemory(obj=0) / metafield / pool / caller_lr | 0x217b058 | 0x215dbc4 |
| `BADBUF` | propsObj / flagsAndSize / buffer / caller_lr | 0x214af20 | 0x2184ef0 |
| `FOREIGN REALLOC` | pool / ptr / size / caller_lr | 0x217350c | 0x214af64 |
| `FOREIGN FREE` | pool / ptr / poolbuf / caller_lr | 0x21aa110 | 0x216f904 |
| `INTEGRITY` | pool / index / vtable+0x11c target / verdict(1=fail) | 0x21f019c | 0x21f026c |

`FOREIGN FREE` fired twice with different value sets — the second was
`hdr[-8] / hdr[-4] / poolsize / idx` at the same site, so it was two
probes at one hook or one probe printing twice.

### Watch slots were armed differently

That build had `w0`–`w3` on `freeInternal`, `userInstantiate`,
`reportVaList` and `reallocCommon`. `main.c` today arms `w0` on
`igStringBufAppend`/`initBootstrap`. The slots themselves are committed;
only the choice of what to point them at differs, which is a one-line
change per slot.

### Values the probes returned, for checking a rebuild

A rebuilt probe should reproduce these on the same build:

```
NULL REGISTRY ENTRY: table=0x0 index=0 byteoff=0 caller_lr=0x215DBC0
NULL TABLE:          dispatchTarget=0 thisObj=0 result=0 result+0x14=0
NULL OBJECT:         obj=0 metafield=0 pool=0 caller_lr=0x215DC18
BADBUF:              propsObj=9 flagsAndSize=0x5FFFF00 buffer=0x7000000 caller_lr=0x2184EF0
FOREIGN REALLOC:     pool=0x3000184 ptr=0x7000000 size=0 caller_lr=0x214AF64
FOREIGN FREE:        pool=0x3000184 ptr=0x7000000 poolbuf=0x30001F0 caller_lr=0x216F900
INTEGRITY:           pool=0x3000184 index=0 vtable+0x11c target=0x217CA84 verdict=1
```

## What else the game build needs, and where it is

- **The dump** (`tfbGame_cafe.rpx`) — not on this laptop. It has to come
  from your own copy of the game again; nothing here can substitute for
  it, by design.
- **The generated C** (213 `game/source/generated_*.c`) — a build
  artefact, never committed. `game/regenerate.sh` reproduces it from the
  dump, and `conquertron/build/recomp` is built and working locally.
- **The game assets** — present on the SD card at
  `/switch/Jouster/content` (`alchemy.xml`, `character/`, `level/`,
  `movies/`, ...), not on the laptop. Worth copying off before anything
  happens to that card.
- **The 2026-08-27 08:09 `.nro` itself** — not on the SD card any more.
  Only its log survives, which is why this note exists.
