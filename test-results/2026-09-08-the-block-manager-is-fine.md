# ARCHBLK: the block manager is fine — the pump is what is missing

Run of 2026-09-08, build `Sep  8 2026 19:43:50`.

```
ARCHBLK allocEarly=2 allocFinal=0 | states seen: zero=37 two=3 other=8
```

**Allocation succeeded both times**, through the `beqlr` exit the previous
probe was not watching. `gotBlock=0` was entirely an artifact of my own hook,
exactly as suspected, and the apparent contradiction with
`getNumAvailableBlocks` reporting 6 was not real.

The state histogram confirms it from the other side: across eight availability
walks, 48 block observations came back **37 in state 0**, 3 in state 2 and 8 in
other states. There are plenty of free blocks. The block manager is not the
problem and should not be looked at again.

## What is left

```
updateArchiveSystem=8   in a 25,200-frame run
startNewTasks=8         every pump reached it
alloc=2                 but it only tried to allocate twice
startBlockRead=1        and only started one read
```

Two questions, and the first is the bigger one:

1. **The archive is pumped eight times in the entire run.** If it needs
   pumping to make progress, eight is nothing.
2. Of those eight, `startNewTasks` only attempted an allocation twice, so
   something inside it declines before reaching the block manager.

## The call chain, from the binary

`updateArchiveSystem` has exactly **one** caller, at `0x2169708`, inside
`igArchive::update` (`0x21696ec`). And nothing in the entire recompiled image
calls `igArchive::update` directly -- there is no `bl 0x21696ec` anywhere -- so
it is reached only through a vtable.

That makes the shape of the bug testable. `igFileContext::update`
(`0x216e638`) is what walks the registered storage devices and calls each
one's `update` virtual. If the context updates every frame while the archive's
update runs eight times, the archive is not in the list being walked, or is
being skipped.

## Next

Build `Sep  8 2026 20:00:02`, md5 `5f5958a3c441be6746a7578a6ac70084`, is on the
Switch. **ARCHDRIVE** counts all three levels of that chain:
`igFileContext::update` → `igArchive::update` → `updateArchiveSystem`.

* **context high, archive 8** -- the archive is not being walked, and the
  question is how storage devices get registered
* **all three at 8** -- the context itself is barely pumped, and the search
  moves up again to whatever should call it each frame
* **context high and archive high** -- the drive chain is fine and the fault is
  inside `startNewTasks`, which is where the `alloc=2` from eight pumps points
  anyway

Worth stating plainly: the graphics layer is confirmed working well enough to
present 1,313 frames, so none of this is a rendering problem. It is a file
loading problem, and the renderer is faithfully drawing the empty scene it has
been given.
