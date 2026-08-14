# Milestone 2 (first attempt): does anything boot at all?

This is **not** the recompiled game, and it's **not built with libnx**. It's
the smallest possible thing that should be a structurally valid Switch
homebrew `.nro`, meant to answer one question on real hardware: does the
packaging pipeline actually produce something hbloader accepts and runs, or
does it error out / crash immediately?

## Why no libnx

devkitPro's site is unreachable from this project's dev sandbox (see the
main README and the Notion project log), so devkitA64 -- and therefore
libnx, which is built against devkitA64's specific newlib toolchain and has
hand-written ARM64 startup/exception-vector code tied to it -- isn't
available here. Porting libnx itself to a different toolchain is a much
bigger job than this milestone, and not something safely done without
hardware to verify against.

What *is* reachable: the source for `switch-tools` (`elf2nro`, `nacptool`,
etc. -- see `github.com/switchbrew/switch-tools`) builds fine as ordinary
host tools with plain `gcc`, no cross-toolchain needed, since they run on
the *development* machine, not the Switch. So the packaging half of the
pipeline is real and NRO-format-correct; the payload is deliberately trivial
so nothing about *running* it depends on libnx.

## What `switch/src/start.s` actually does

Per the [Homebrew ABI](https://switchbrew.org/wiki/Homebrew_ABI), hbloader
calls the entry point with `x0`=env context pointer, `x1`=`0xFFFFFFFFFFFFFFFF`,
`lr`=return address into the loader. A well-behaved app returns to that `lr`
with `x0`=an error code. This program does exactly that and nothing else:

```
mov x0, xzr
ret
```

libnx's own `crt0` (`switch_crt0.s` in the libnx repo) does self-relocation
via a MOD0 header before calling into C, because a real program has
data/GOT/`.rela.dyn` references that need fixing up once loaded at an
ASLR-chosen base address. MOD0 is *not* something the OS loader itself
requires -- it's crt0's own bookkeeping for relocating itself. This program
has no data references, no calls to other addresses, nothing position-
dependent, so there's nothing to relocate and MOD0 is safely omitted.

`switch/link.ld` is a from-scratch linker script (not libnx's `switch.ld`)
that produces exactly the program-header shape `elf2nro` requires: 3
`PT_LOAD` segments (code/rodata/data) followed by a 4th header it reads
purely to compute `.bss` size. Confirmed locally with `readelf -l`.

## What's actually been verified vs. not

Verified on this machine, without hardware:
- The linker produces exactly the 4-phdr shape `elf2nro` expects.
- `elf2nro` and `nacptool` run cleanly against it with no errors, producing
  a file with a well-formed `NRO0` header and `ASET`/nacp block.
- The first 8 bytes of the file decode to the expected `mov x0, xzr` / `ret`
  instruction encoding.

**Not** verified, because it requires your hardware:
- Whether hbloader actually accepts and loads this file.
- Whether the entry/return convention as implemented here is complete
  enough in practice (there could be an undocumented loader expectation
  this misses).

If this doesn't boot, that's useful information, not a wasted test --
put whatever you see in `switch/test-results/`.

## Building

```sh
switch/build.sh
```

Output: `switch/build/bramble_poc.nro`. Copy it to
`/switch/Bramble/bramble_poc.nro` on your SD card.
