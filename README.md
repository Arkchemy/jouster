# jouster

The Nintendo Switch side of [Arkchemy](https://github.com/Arkchemy): it takes
the C emitted by [conquertron](https://github.com/Arkchemy/conquertron) and
builds it into a real Switch homebrew `.nro`.

No PowerPC emulation runs at runtime — the recompiled game is compiled to
native ARM64 and linked against libnx like any other homebrew app.

This repository contains no game code and no game assets. You supply your own
legally-dumped copy.

## Layout

| Path | What it is |
| --- | --- |
| `game/` | The full-game build: `Makefile`, `regenerate.sh`, and `source/main.c` (the harness/entry point). The recompiled `generated_*.c` are build artefacts and are not committed. |
| `native/` | Smaller native test programs |
| `gx2_test/` | GX2 → deko3d graphics experiments |
| `src/start.s` | Bare-metal ARM64 startup used by the earliest no-libnx milestone |
| `link.ld`, `build.sh` | Linker script and build script for that same bare-metal path |
| `test-results/` | Dated real-hardware test logs |
| `docs/` | Historical milestone notes |

## Building the game

Requires [devkitPro](https://devkitpro.org/) with devkitA64, libnx, and
deko3d, plus a checkout of
[conquertron](https://github.com/Arkchemy/conquertron) — it supplies
`ppc_runtime.h`, the `cafeos_*.h` shims and `cafeos_state.c` that the
generated C compiles against.

You do not have to fetch it yourself. `tools/fetch-conquertron.sh` resolves
conquertron in this order:

1. an explicit `make CONQUERTRON=/path/to/conquertron`
2. a sibling checkout next to this repo
3. otherwise, a copy fetched into `deps/conquertron`, pinned by
   `conquertron.lock`

A sibling checkout deliberately beats the fetched copy. conquertron and jouster
are developed together, and a fetched tree silently shadowing local recompiler
edits would mean the next hardware run tests the wrong code. An
already-present copy is used with no network access at all, so builds work
offline.

The side-by-side layout, if you want it:

```
some-dir/
  conquertron/
  jouster/
```

Two helpers, since a sibling and a vendored copy look identical in build
output and building against the wrong one is silent:

```sh
make -C game conquertron-info      # resolved path, commit, local modifications
make -C game conquertron-update    # update deps/conquertron and re-pin the lock
```

```bash
export DEVKITPRO=/opt/devkitpro
cd game
make -j
```

If your checkout is laid out differently, point it at conquertron explicitly:

```bash
make -j CONQUERTRON=/path/to/conquertron
```

That produces `game/Jouster.nro`. Copy it to your Switch's SD card under
`/switch/Jouster/`.

The generated C is not in the repository — `game/regenerate.sh` drives
conquertron against your own dump to produce it before building.

### Without a local devkitPro install

The devkitPro toolchain also ships as a container image, which builds
everything here without touching your system package manager (podman or
docker, run from the directory that holds both checkouts):

```bash
podman run --rm -v "$PWD":/work:z -w /work/jouster/native --userns=keep-id \
    docker.io/devkitpro/devkita64 \
    bash -lc 'export PATH=$DEVKITPRO/devkitA64/bin:$PATH; make -j'
```

Swap `native` for `gx2_test` or `game` to build the others; the image
already carries libnx and deko3d, so nothing else needs fetching.


## Test harness

`game/source/main.c` is not a normal entry point; it is a diagnostic harness.
It runs the recompiled game on a worker thread while the main thread logs
periodic checkpoints to `sdmc:/switch/Jouster/game-results.log`, including
memory-allocation events, watched function arguments, and a stall detector
that exits early if execution stops making forward progress. Test duration can
be overridden by writing a number of seconds to
`sdmc:/switch/Jouster/test-seconds.txt`.

Those logs are how nearly every bug in this project has been found; dated
examples live in `test-results/`.

## Status

Early, and not playable. The engine starts, runs its 114 static initialisers
and gets partway through its reflection registration before stalling, so it
never reaches level or asset loading.

What does work:

- **Video and audio playback.** `bash.mov` plays start to finish, 526 frames at
  29.97fps with audio in sync, decoded by ffmpeg from devkitPro's portlibs. A
  native Bink shim also serves the game's own `BinkOpen`/`BinkDoFrame` API,
  delivering all 720 luma rows per frame.
- **The Wii U boot presentation** — the `bootTvTex.tga` splash and the
  18.9-second `bootSound.btsnd` jingle, from the game's own `meta/` files.
- **Filesystem access.** All 22 coreinit FS imports the game calls are
  implemented, and a boot self-test opens `/vol/content/alchemy.xml`,
  `content:/alchemy.xml`, a bare relative path and a nested
  `permanent/bootstrap.bld` through the same translation the engine uses.

What does not:

- **The engine boot.** Registration reaches roughly 99 of about 1,007 classes.
  Two filesystem classes, `igFile` and `igVirtualStorageDevice`, still never
  register.
- **Game rendering.** Graphics calls are honest no-ops pending a Switch
  backend; nothing the game itself draws reaches the screen.

Progress is tracked run by run in `test-results/`, including the wrong turns.
Several confident theories in there were later disproved and the records say
so rather than being quietly rewritten -- the pool allocator, for one, was
chased for hours before it turned out to be reading a block header out of
address 0.

## Reference material

- `docs/registration-order-real.txt` -- 965 classes in the retail game's own
  registration order, captured from Cemu by breaking on `appendToArkCore`.
  Diffing our order against it is what identified the missing classes.

## Licence

See [`LICENSE`](LICENSE) — Arkchemy Free & Source-Available License v2.0. It is
**not** an OSI-approved open source licence and some uses require permission,
so please read it before reusing anything here. Contact details and the
project Discord are in [`llms.txt`](llms.txt).

Contributors are listed in [`CONTRIBUTORS.csv`](https://github.com/Arkchemy/woodburrow/blob/main/CONTRIBUTORS.csv); the codename
scheme is explained in [`CODENAMES.md`](CODENAMES.md).
