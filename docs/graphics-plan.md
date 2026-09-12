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

### 2. Vertex and pixel shaders — blocked on one document

The containers, layout and inventory are all established:

* Format is GFD (`Gfx2`) v7.1, parsed by `blaster/gfd.py`.
* Layout is CF section at byte 0, clauses from byte 256 — measured across all
  five shaders in the executable, every one identical in shape.
* Word order is **little-endian**. An earlier note here said big-endian and
  was wrong: that test scanned whole programs for end-of-program bits, where
  most words are ALU and TEX and bit 21 means something else, so it counted
  noise. Scanning the CF section alone gives zero end-of-program bits
  big-endian -- impossible -- and exactly one little-endian. Confirmed again
  by decoding: binkPixelShader's first CF instruction reads as TEX, count 3,
  address 0x30, and 0x30 x 8 is byte 384, exactly where the region scan
  independently found three 16-byte texture fetches.
* Struct offsets confirmed **against hardware**: `GX2VertexShader` size at
  `0xD0`, program at `0xD4`; `GX2PixelShader` at `0xA4`/`0xA8`. A run reported
  size 440 with first words `00000000 00008009`, byte for byte the
  `defaultVertexShader` extracted statically.
* Texture-fetch counts were predicted before they were measured: Bink's pixel
  shader samples 3 planes (YUV), its alpha variant 4, the default shader 1.
  Three for three.

What is missing is the **AMD R600/R700 ISA document**. The per-instruction
encodings are not on this machine — checked, including Mesa headers and the
DRM interface. Writing a decoder from memory produces output that looks like
a disassembly and is wrong in ways nobody notices for a week, which is the
same failure that cost a hardware cycle twice in the week before this was
written. So this piece is **deliberately not started** rather than started
badly.

### 3. State and textures — mechanical

Viewport, scissor, context state, samplers, textures, uniform registers. About
fifteen entry points carrying most of the call volume, each a fairly direct
mapping onto deko3d state. Tedious, not hard, and unblocked.

Note `mode=0` on every shader bound: uniform **registers**, not uniform
blocks. That is the simpler path.

### 4. Display lists — an open question that changes the answer

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

1. **Answer the display-list question.** One run. Everything else depends on it.
2. **Fetch shaders → deko3d attribute state.** Unblocked, self-contained, and
   removes 8,957 calls a run from the problem.
3. **State and textures.** Mechanical, and testable before any shader works,
   because a draw with a stub shader still proves the state path.
4. **Shader translation**, once the ISA reference is to hand.

## The first picture worth aiming at

Not a level. The **boot video** — the owner's own stated milestone, and the
smallest complete target in the whole surface: `binkVertexShader` (6 CF
instructions, no textures) and `binkPixelShader` (3 CF instructions, 3 texture
fetches for Y, U and V). Both are already extracted and structurally
understood. Getting those two running draws a frame of video, which is a real
picture from the game's own data, and exercises every part of the path above.
