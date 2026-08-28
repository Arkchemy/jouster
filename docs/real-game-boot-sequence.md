# What the real game does at boot

Captured 2026-08-28 from Cemu 2.6 running the owner's own dump, with
Cemu's own API logging enabled (`<logflag>` in settings.xml, restored to
0 afterwards). This is a reference trace of the real Wii U title, not
anything this project produced.

## The file sequence, in order

```
/vol/content/alchemy.xml                     -> 0   (opened first, succeeds)
/vol/content/permanent/bootstrap.arc         -> -6  (absent; the game tries .arc first)
/vol/content/permanent/bootstrap.bld         -> 0
/vol/content/item/legal.arc                  -> -6
/vol/content/item/legal.bld                  -> 0
/vol/content/permanent/global.arc            -> 0
/vol/content/permanent/global.bld            -> 0
/vol/content/character/init_setup.arc        -> -6
/vol/content/character/init_setup.bld        -> 0
/vol/content/level/title.arc                 -> 0
```

Two things fall straight out of this.

**alchemy.xml is opened first, before anything else.** It is not merely
configuration that gets read at some point -- it is the first file the
engine touches. Our run never opens it, or any other file.

**The .arc/.bld pairing is a real pattern, and -6 is normal.** The game
asks for `X.arc`, accepts failure, then opens `X.bld`. A shim that treats
a missing .arc as an error rather than an expected miss would derail the
sequence immediately.

## Why this matters for the current bug

The investigation has been stuck on `igHandlePool::_handleList` being
null when `igMemoryContext::activate` uses it. Everything about the
object, its class, its metaobject and its registration checked out, so
the remaining question was what assigns that field in the real game.

This trace says the engine has already read alchemy.xml and
bootstrap.bld before it gets anywhere near where ours dies. Those files
are the reflection system's own metadata and configuration -- exactly the
input that would populate the structures our run has empty.

So the ordering fault is upstream of everything measured so far: the real
engine loads its metadata first, and ours never asks for a file at all.

## Method note

Cemu with `<logflag>` set is a far better instrument than the hardware
probe loop for questions about *the real game's* behaviour: it runs at
full speed, needs no rebuild, no NRO copy, and no probe design. A short
capture produced 1.4M lines covering every coreinit call.

`--enable-gdbstub` also exists and was tried first. A breakpoint on
`0x2165878` was accepted but never fired while the game booted to the
title screen, and the stub reported an initial `pc` well outside the
RPX's address range, so that route needs a positive control (break on
something that must execute, such as `main` at `0x2002bf0`) before
anything measured through it can be trusted. `--force-interpreter`, which
that route needs, also makes the game nearly unplayable.

## What this trace can and cannot tell us

Cemu's logging is partial. The capture contains only 46 distinct
coreinit functions, and just three MEM ones:

  MEMGetBaseHeapHandle, MEMAllocFromFrmHeapEx,
  MEMGetAllocatableSizeForFrmHeapEx

`MEMAllocFromDefaultHeapEx`, `MEMAllocFromExpHeapEx` and
`MEMCreateExpHeapEx` do not appear anywhere in the log -- not because the
game does not call them, but because Cemu does not instrument them for
logging. An earlier reading of this trace nearly recorded "the real game
performs almost no heap allocation before opening its config, while ours
burns 116MB", which would have been a false finding drawn from missing
instrumentation rather than from behaviour.

So this trace is authoritative about what it does log -- FSOpenFile is
instrumented, and the file sequence is real -- and silent about
everything else. Comparisons of allocation behaviour need a different
instrument.

## Standing method

  Questions about the REAL game  -> Cemu, with <logflag> set. Minutes,
      no rebuild, full speed. Check first whether the API in question is
      actually instrumented before drawing conclusions from its absence.

  Questions about OUR build      -> the hardware probe loop. Slower, but
      it is the only thing that observes what we actually produce.

  The gdbstub route needs a positive control before it can be trusted;
  a breakpoint on a known-executing address should be verified to fire
  before any result from it is believed.
