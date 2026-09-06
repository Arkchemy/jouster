# 0x1C is an unresolved igz pointer, not a pool index

2026-09-06, with bone in the Discord.

## What 0x1C actually is

An igz pointer is a packed 32-bit word:

* **top 10 bits** -- the memory pool (bone: *"the first 1.25 bytes in ssa wii u"*)
* **low 22 bits** -- the offset within it

So `0x0000001C` is **pool 0, offset 0x1C**, and bone adds that it *"should
always be the first igMemoryRefMetaField pointer that any file uses, because
of the first igObjectList"*.

The recompiled code confirms the layout independently. Two call sites decode
it correctly:

```
215c280: lwz  r0, 4(r3)
215c28c: srwi r4, r0, 0x16      ; >> 22, taking the top 10 bits
215c290: b    getMemoryPoolByIndex
```

`>> 22` is exactly "the top 10 bits", and that path works -- it yields index 2
for our objects and resolves a real pool.

## Where ours goes wrong

The LZMA allocator does not decode anything. It reads a global and passes the
whole word:

```
216b700: lis r3, 0x1013         ; &.bss+306684
216b708: lwz r3, 0xdfc(r3)      ; -> 0x0000001C
216b710: bl  0x217d3f8          ; used as a pool
```

The address folding is correct -- checked against the working site, where the
same `lis`+displacement pattern folds consistently -- so the load is faithful
and **the global really does contain `0x0000001C`**.

Which means the value was never fixed up. `getMemoryPoolByIndex(0x1C)` asks
for pool 28; the real pools are 0..5 (bone: `0, 8, 10, 18, 20, 28`, spaced 8).
It returns NULL, LZMA cannot allocate its 15,980-byte probability array, and
the archive never drains.

## So the bug is a missing igz pointer fixup

An igz carries fixup tables naming the words that must be converted from
packed `(pool, offset)` form into real addresses at load time. We extracted
them from `bootstrap.bld` and they are present: **RVTB**, **ROFS**, **RSTR**,
**ROOT**, plus **EXNM** and **EXID** further in.

Nothing in our boot applies them to this field, so the raw packed value
survives into a global that the LZMA allocator later reads as a pool.

That also explains, at long last, why every memory-corruption probe came back
empty. There is no corruption. `RELWINDOW n=0`, `WIPE n=0`, `OVERLAP` no
overlap, `OWNER n=2` with a clean alloc/free/alloc -- all correct, because
nothing was ever corrupted. A pointer simply was not translated.

## Next

Find where the fixup pass should run, and whether it runs at all. The tables
are in the file; the question is whether the loader walks them. The archive
side is now readable end to end (`blaster/igarchive_extract.py`), so the
expected post-fixup value can be computed from the file rather than guessed.
