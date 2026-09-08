# DEVLOG: the archive registers, decompresses one block, and is closed

Run of 2026-09-08, build `Sep  8 2026 20:44:43`.

```
DEVLOG open=2 reachedRegister=1 close=2 n=2
 [ADD dev=0x829a610 vt=0x109ed8 @3079012]
 [REM dev=0x829a610 vt=0x109ed8 @3086931]
```

The archive **is** registered -- so `reachedRegister=1` settles the third
possibility from last time -- and then removed again about 7,900 calls later.
The single LZMA inflate sits between them, at roughly call 3,085,008. So the
order is: open, register, decompress one block, close, deregister.

That explains every downstream number without needing anything else:
`igArchive::update` runs 8 times because after `@3086931` the archive is not in
the list to be walked, `startNewTasks` never runs again, and 131,072 of 198,695
bytes is simply where the reading stopped.

## What closes it

`igArchive::close` is in **no vtable** -- it is never dispatched virtually --
and has exactly two direct callers:

```
0x2000050  in .text, unnamed (a thunk)
0x2000aa8  tfbGame::streamContext::load(const char *)
```

So the close comes from **game logic**, not from the engine's file plumbing.

## Why this is not yet a conclusion

Two things have to be established before that means what it looks like:

* **`open=2`.** Two archives were opened and only one registered. The
  add/remove pair is for `0x829a610`, but nothing so far says that object is the
  boot archive rather than something else.
* **"streamContext" reads as audio.** A stream context opening an archive,
  taking what it wants and closing it may be entirely correct behaviour, and the
  boot archive's own loading may be failing for an unrelated reason.

Calling this the bug now would repeat the mistake made twice already this week
-- the `[pool+0x50]` reading and the `+0x20` flag -- where a plausible chain was
accepted before the object at the end of it was identified.

## Next

Build `Sep  8 2026 21:00:39`, md5 `97b5e073fe452a46367b1848f3622e08`, is on the
Switch.

**ARCHNAME** records, per open, the archive's `this` and the **filename** it was
opened with, plus the `lr` that closed it. Names settle what counts cannot:

* **the closed archive is the boot archive** -- then `streamContext::load`
  closing it mid-load is the bug, and the question is what it thinks it has
  finished
* **the closed archive is an audio or stream file** -- then this whole path is
  a correct sequence for a different archive, the boot archive is elsewhere,
  and the search restarts from `open=2 reachedRegister=1` with the right object
  in hand

Either way the measured chain to the blank screen is unchanged, and none of it
is a rendering problem.
