# Correction: the manager is constructed correctly, then zeroed at call 440,611

Run: build `Sep  6 2026 13:52:58`, second run, `watch.cfg` dumping both
managers.

**This supersedes `2026-09-06-default-manager-is-an-early-snapshot.md`, which
was wrong.** That file concluded the default manager was a snapshot taken
before the registrations finished. It is not. It is built correctly and then
destroyed.

## What the dump shows

`0x45f3964` over the run, one line per state change:

```
frame 0      ZEROS
frame 2940   OTHER    00:045002e0 04:045002e0 ... 14:045f395c 18:00040006
frame 3060   MANAGER  00:0011ecd8 04:00800001 08:046439c8 0c:00000002 ...
frame 3720   ZEROS    (and stays zeroed for the remaining ~21,000 frames)
```

At frame 3060 the block holds a real `igMemoryPoolFrameManager`: vtable
`0x0011ecd8`, the **same vtable** as the healthy manager `0x4502fb0`, with a
frame stack at `0x046439c8` and top index 2. It is correct. Then at frame
3720 it is zeroed and never restored.

The healthy manager, for comparison, never changes:

```
DUMP2 0x4502fb0 00:0011ecd8 04:00800001 08:04502fc8 0c:00000003 ...
```

## Why the previous read was wrong

`LZWALK ... frames=1 maxCount=0x0 slot28=0` was interpreted as "one frame
holding zero pools". It is not. Walk the code in `generated_0159.c` against
an all-zero block:

```
[mgr+0xc] = 0        -> __fr = 0        -> frames = __fr+1 = 1
[mgr+0x08] = 0       -> __st = 0        -> __arr = 0
for (__f = __fr; __f >= 0 && __arr; __f--)   -> never runs, __arr is 0
                                             -> maxCount stays 0, slot28 stays 0
```

**A zeroed block and a valid-but-empty manager produce byte-identical LZWALK
output.** That ambiguity is what sent the last write-up down the wrong path,
and the ORDERING timestamps happened to fit the wrong story well enough to
look like confirmation.

## Who zeroes it

```
loopwatch(storewatch_value)     hits=3@440611 changed=3 last=0x0
loopwatch(storewatch_writer_pc) hits=3@440611 changed=2 last=0x21565ac
loopwatch(storewatch_lr)        hits=3@440611 changed=3 last=0x215dd0c
loopwatch(storewatch_r29)       hits=3@440611 changed=3 last=0x45f35dc
loopwatch(storewatch_r31)       hits=3@440611 changed=3 last=0x62
```

Three writes, all at call **440,611**, the last value **zero**. `lr=0x215dd0c`
is the instruction after `igObjectList::append`'s call into
`igDataList::resizeAndSetCount` at `0x215dd08`.

So the original instinct across the earlier sessions -- "resizeAndSetCount
clobbers the manager" -- was right about the site. The `WIPE` probe missed it
because `WIPE` watched *moves* whose destination range covered the address,
and this is not a move: it is the **zero-fill** in `setCount`, a direct store
of 0.

`r29 = 0x45f35dc` is the buffer being filled. The manager sits `0x388` bytes
(904) inside it.

## What this means

The block was a live, correctly-built frame manager, and a list resize then
zero-filled straight through it. Either

* the allocator handed the same memory to both the manager and that list's
  element buffer -- a double allocation, our bug; or
* the manager was legitimately freed and the engine is meant to re-register a
  new default afterwards, which we never do.

Over-release was ruled out in an earlier session (the refcount never reaches
zero), which points at the first.

## Next: Cemu, not more hardware rounds

The question is now sharp enough to put to the real game: **at the equivalent
point, does guest `0x45f3964` in retail hold a frame manager that survives
this list append?**

* survives -> our allocator handed out live memory; the bug is in jouster's
  allocator and the fix is there
* also reused -> the manager pointer we keep is stale by design and we are
  missing a re-registration

Read it out of Cemu via `/proc/<pid>/mem` at full JIT speed, per
[[cemu-guest-memory-reading]] -- anchor on `"ram:/alchemy.xml\0"` at guest
`0x10051308` to derive the base, then poll `0x45f3964` and `0x45f35dc`. Do
**not** use the gdb stub; it has cost three sessions already.

---

## Cemu attempt, and why the obvious version of it does not work

Tried it. The tooling is fine -- `tools/cemu-peek.py` derives the base from the
`"ram:/alchemy.xml\0"` anchor at guest `0x10051308`, the anchor verified, and
reads come back correct. **The comparison itself is invalid as posed.**

`0x45f3964` is a *jouster* address. jouster runs in a flat 1 GB `guest mem[]`
array, so its heap sits around `0x045xxxxx`. Retail uses the Wii U map, and its
heap pointers read back around `0x117cca00`. Reading `0x45f3964` out of Cemu
reads unmapped space and returns zeros -- which looks exactly like "the manager
is zeroed in retail too" and would have confirmed whatever you already believed.

The static segments *do* correspond. jouster's `.data` base is 8192 (its
"guest 13528" is `.data+5336`), retail's is `0x10000000`, so the delta is
`0x0FFFE000`, and jouster's metaobject `0x0011ecd8` maps to retail
`0x1011ccd8`. That lands on real structured data -- three separate metaobjects
each holding a pointer to their own address minus `0xE8`, which is not what a
wrong mapping produces.

The heap does **not** follow that delta, because it is allocated at runtime.
So finding retail's frame managers means scanning for the metaobject pointer
rather than computing an address. That scan over `0x10000000-0x1C000000` found
**zero** instances -- but that result is not usable either, because the retail
game was not driven past boot: the job-queue head at `0x10136a00` had `+8`
pointing at itself (empty) and nothing changed over a 3-second sample while
Cemu sat at 283% CPU. Almost certainly parked on a title screen waiting for
input that was never sent.

**What Cemu needs before it can answer this**, in order:

1. Drive the title past boot -- controller input, or start it from a save.
2. Confirm the game has reached archive loading (watch `0x10136a00` change).
3. Locate retail's frame managers by scanning for metaobject `0x1011ccd8`,
   not by translating a jouster heap address.
4. Then watch whether any of them is zeroed the way ours is at call 440,611.

The mapping fact is the reusable part: **never compare jouster heap addresses
to retail ones directly.** Statics map with `+0x0FFFE000`; the heap does not
map at all.
