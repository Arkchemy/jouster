# Graphics: what is known, what it costs, and what is blocked

Written 2026-09-12, the day the engine first issued draw calls. Rendering is
now the critical path rather than loading, and this is the shape of it.

Everything here is measured unless it says otherwise.

## What the engine actually does

From one 900-second run, instrumented:

```
570,113 GX2 calls across 72 entry points
  8,581 GX2DrawEx          0 GX2DrawIndexedEx
  4,232 present            7 distinct shaders bound, 57,438 times
```

**Seven shaders.** Three vertex, four pixel, bound across the whole run. Not
seven hundred. That is the single most encouraging number in the project:
a first picture does not need a general R600 translator, it needs seven
programs of a few hundred bytes each.

**No indexed draws at all.** Every draw so far is `GX2DrawEx`.

## The four problems, and they are not the same problem

### 1. Fetch shaders — no translation needed

`GX2InitFetchShaderEx` is called 8,957 times. A Wii U fetch shader is
*generated* microcode that pulls vertex attributes out of buffers, because
that GPU has no fixed-function attribute fetch.

deko3d does. Vertex attribute state there is **declarative** — formats,
offsets, strides, divisors. So a fetch shader is not translated at all; it is
read as the attribute description it was generated from and rebuilt as
deko3d state.

This is the cheapest of the four and it is entirely unblocked.

### 2. Vertex and pixel shaders — translated, not yet wired up

The containers, layout and inventory are all established:

* Format is GFD (`Gfx2`) v7.1, parsed by `blaster/gfd.py`.
* Layout is CF section at byte 0, clauses from byte 256 — measured across all
  five shaders in the executable, every one identical in shape.
* Word order is **little-endian**. An earlier note here said big-endian and
  was wrong: that test scanned whole programs for end-of-program bits, where
  most words are ALU and TEX and bit 21 means something else, so it counted
  noise. Scanning the CF section alone gives zero end-of-program bits
  big-endian -- impossible -- and exactly one little-endian.
* Struct offsets confirmed **against hardware**: `GX2VertexShader` size at
  `0xD0`, program at `0xD4`; `GX2PixelShader` at `0xA4`/`0xA8`. A run reported
  size 440 with first words `00000000 00008009`, byte for byte the
  `defaultVertexShader` extracted statically.
* Texture-fetch counts were predicted before they were measured: Bink's pixel
  shader samples 3 planes (YUV), its alpha variant 4, the default shader 1.
  Three for three.

**This is no longer blocked.** The AMD R700 ISA document arrived 2026-09-12
and `blaster/r600.py --cf` now disassembles CF, TEX and ALU clauses;
`blaster/r600_glsl.py` translates a decoded program into GLSL, and
`blaster/build-shaders.sh` compiles each one with devkitPro's `uam` into a
deko3d `.dksh` module. **All seven programs the engine actually binds
translate and compile**, as do the five extracted from the executable.

The seven cover a small corner of the ISA -- 21 distinct ALU opcodes, one
`if`/`else`, and two texture-fetch forms. Three of those opcodes needed real
work rather than a table entry:

* `DOT4` is a *reduction*: one result spread across four slots, each carrying
  a pair of components, and the doc is explicit that only `PV.x` holds it.
  Translated as four independent multiplies it yields four unrelated numbers.
* `CNDE_INT` compares the bit pattern, not the float, so it goes through
  `floatBitsToInt`.
* `MULADD_D2` carries its output modifier in the opcode, because OP3 has no
  `OMOD` field to put one in.

The evidence that the decode is right is that the shaders come out as the
programs their names claim: `binkPixelShader` is a YUV-to-RGB conversion,
`binkAlphaPixelShader` the same with a fourth sample supplying alpha instead
of a literal 1.0, and both vertex shaders a four-row matrix multiply with a
texcoord passthrough. See `blaster/README.md` for the two places the ISA
document is wrong and how the shaders settled each one.

What remains for this item is the **runtime half**, which is a jouster problem
rather than a blaster one:

* ~~deko3d has to be initialised at all.~~ **It already is** — `GX2Init`'s
  shim builds a real swapchain, and the bridge already covers rasterizer,
  depth-stencil, colour and multisample state, sampler and texture descriptor
  pools, render targets and copies. An earlier version of this document said
  otherwise and was simply wrong about the tree it describes. The gap is
  shaders and draws, not deko3d.
* ~~Recognising which shader a pointer refers to.~~ Done, 2026-09-12:
  `cafeos_gx2_shaders.h` hashes the program bytes on bind and opens the module
  named after the hash. Identity has to be content, not the pointer — the same
  shader is re-bound 57,438 times a run from allocator-chosen addresses, and
  two of the seven programs are both exactly 400 bytes and different shaders.
  The hash is FNV-1a 64 rather than SHA-256 so that both ends can compute it:
  nine lines in the shim, one in `build-shaders.sh`.
* ~~The modules have to ship in romfs.~~ They go on the SD card instead, in
  `switch/Jouster/modules`, pushed by the courier like any other build output.
  That avoids making jouster's Makefile depend on blaster. Note the directory:
  putting them in `switch/Jouster/shaders` beside the dumps makes the courier
  pull them straight back as if the console had produced them.
* **Drawing with them.** `GX2DrawEx` is still a no-op. This is the next step,
  and it is deliberately *after* a run confirms the modules load: a draw path
  built on modules deko3d turns out to reject costs a day of looking in the
  wrong place.
* ~~Two mappings are assumed~~ — **both confirmed on hardware, 2026-09-12**,
  by the `FETCHATTR` and `UNIFREG` probes. See below.

### 2a. The two mappings, measured

Both of the translation's assumptions were probed on hardware rather than
reasoned about, and both hold.

**Attribute *N* arrives in GPR *N+1*.** `FETCHATTR` dumps the
`GX2AttribStream` array raw — eight words per attribute, assuming only the
32-byte stride. The whole game uses two configurations:

```
set0  location=0 -> R1  buffer=0  offset=0   FLOAT_32_32_32     per-vertex          dest=(x,y,z,1.0)
      location=1 -> R2  buffer=0  offset=12  FLOAT_32_32        per-vertex          dest=(x,y,0.0,1.0)
set1  location=0 -> R1  buffer=15 offset=0   FLOAT_32_32_32_32  per-instance div=1  dest=(x,y,z,w)
      location=1 -> R2  buffer=0  offset=0   FLOAT_32_32_32     per-vertex          dest=(x,y,z,1.0)
      location=2 -> R3  buffer=0  offset=12  FLOAT_32_32        per-vertex          dest=(x,y,0.0,1.0)
```

The field order — location, buffer, offset, format, type, aluDivisor, mask,
endianSwap — is confirmed by the data rather than by a header, which matters
because wut is not on this machine. A three-float attribute at offset 0 is
followed by one at offset 12; the formats are the four `FLOAT_32*` values and
nothing else; the dest-select masks decode to exactly the component patterns
those formats need. A wrong stride or field order does not produce that.

The mapping itself is then confirmed by the shaders agreeing with the state:
`binkVertexShader`'s first instruction group is `R1.w * C3.*`, the
translation column of the matrix, which is only correct if `R1.w` is 1.0 —
and the fetch state gives position a dest select of `(x, y, z, 1.0)`. Its
`MOV R0.x = R2.x` / `MOV R0.y = R2.y` is a two-component passthrough, and
`R2` is the `FLOAT_32_32` texcoord. Two independently measured things agree.

**Uniform-register offsets are u32 words, so constant-file index *n* is at
offset *4n*.** `UNIFREG` recorded 1,147 vertex and 725 pixel calls across
eight distinct shapes:

```
[vs off=0 count=16] [vs off=16 count=16] [vs off=16 count=4] [vs off=32 count=4]
[ps off=0 count=4]  [ps off=4 count=4]   [ps off=8 count=4]  [ps off=16 count=4]
```

`count=16` at offset 0 is sixteen words — four `vec4`s, a 4x4 matrix, which is
exactly what both vertex shaders read as `C0`-`C3`. `defaultVertexShader` also
reads `C4`-`C7`, and there is the second matrix at offset 16. The pixel
shaders take single `vec4`s at 0, 4, 8 and 16. Had the unit been a `vec4`
instead, every uniform index in the generated GLSL would be four times out.

So `uf[n]` in the generated shaders is the `vec4` the game set, unchanged.

### 3. State and textures — mechanical

Viewport, scissor, context state, samplers, textures, uniform registers. About
fifteen entry points carrying most of the call volume, each a fairly direct
mapping onto deko3d state. Tedious, not hard, and unblocked.

Note `mode=0` on every shader bound: uniform **registers**, not uniform
blocks. That is the simpler path.

### 4. Display lists — answered: the engine does both

`GX2CopyDisplayList` is called 42,302 times a run while
`GX2BeginDisplayListEx` and `GX2EndDisplayList` are both zero. Those cannot
all be true of a program that builds its own lists.

Either the engine uses the non-`Ex` pair, or it is **replaying lists built
elsewhere** — prebuilt GPU command streams in the game's own data. The
difference is the whole shape of the work:

| | if immediate calls | if prebuilt lists |
| --- | --- | --- |
| what is translated | ~15 GX2 functions and 7 shaders | a raw R600 command stream |
| rough scale | weeks | a quarter, and a different project |

`GX2LISTS` names every display-list and draw entry point with its count,
called or not, so a single run settles it. **Nothing should be built on top of
this until it is answered.**

**Answered, 2026-09-12.** A `GX2LISTS` run reports `GX2BeginDisplayListEx` 0,
`GX2EndDisplayList` 0, `GX2CopyDisplayList` 43,202 — so the engine does not
build lists, it replays prebuilt ones, *and* issues 8,581 immediate `GX2DrawEx`
calls alongside them. Both branches of the table above are real, which means
the cheap one is also the right one to start with: the immediate draws are a
complete path from vertex buffer to pixel, and they are the branch the
translated shaders serve. The prebuilt command streams can wait until
something is on screen.

## Where the shaders come from

Two sources, which is worth knowing because it is easy to assume one.

Five are linked into the executable — the GX2 workflow compiles a shader into
a C byte array, and the compiler left the source paths behind
(`gfx/defaultVertexShader.c`, `movie/binkMovie/binkPixelShader.c`, and three
more). `blaster/gfd.py` pulls them straight out of `.rodata`.

The rest are not. Runtime binds programs of 496, 488, 1296 and 528 bytes and
none of those sizes exist in the binary; `bootstrap.bld` contains no GFD
containers either, so they come from other archives or are built at load time.
Rather than chase each source, the game now writes every program it binds out
of GPU memory to `switch/Jouster/shaders`, and the courier collects them.

## The order to do this in

1. ~~**Answer the display-list question.**~~ Answered by a run:
   `GX2BeginDisplayListEx` 0, `GX2EndDisplayList` 0, `GX2CopyDisplayList`
   42,302. The engine replays prebuilt lists *and* issues 8,581 immediate
   draws, and the immediate draws are the branch worth translating first.
2. ~~**Shader translation.**~~ Done for all five shaders in the executable —
   `blaster/build-shaders.sh` produces `.dksh` modules. Was item 4; the ISA
   document arriving moved it.
3. **Stand deko3d up in jouster at all**, and load one translated module. This
   is now the gating step: everything above it is host-side work that has
   never run on the console.
4. **Fetch shaders → deko3d attribute state.** Self-contained, and removes
   8,957 calls a run from the problem. Also settles the GPR-to-attribute
   mapping the vertex translation currently assumes.
5. **State and textures.** Mechanical, and testable before any shader works,
   because a draw with a stub shader still proves the state path.

## The first picture worth aiming at

Not a level. The **boot video** — the owner's own stated milestone, and the
smallest complete target in the whole surface: `binkVertexShader` (6 CF
instructions, no textures) and `binkPixelShader` (3 CF instructions, 3 texture
fetches for Y, U and V). Both are already extracted and structurally
understood. Getting those two running draws a frame of video, which is a real
picture from the game's own data, and exercises every part of the path above.
