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

The build defaults to conquertron being cloned next to this repo:

```
some-dir/
  conquertron/
  jouster/
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

Early. The game boots and runs its engine startup sequence, but nothing is
rendered yet, no audio plays, and no level or asset loading has been reached.
Graphics calls are honest no-ops pending a Switch backend. Expect it to run for
a while and then stop, not to be playable.

## Licence

See [`LICENSE`](LICENSE) — Arkchemy Free & Source-Available License v2.0. It is
**not** an OSI-approved open source licence and some uses require permission,
so please read it before reusing anything here. Contact details and the
project Discord are in [`llms.txt`](llms.txt).

Contributors are listed in [`CONTRIBUTORS.csv`](CONTRIBUTORS.csv); the codename
scheme is explained in [`CODENAMES.md`](CODENAMES.md).
