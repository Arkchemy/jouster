# The pool is never reset -- and two probe mechanisms were misread

Run of 2026-09-06 (build 12:22:57), watch on the pool object at 0x4500274.

## The pool-reset theory is dead

The watch on the pool's own vtable fires **3 times, all at call 2,037**:

    memwatch [1]@2036 = 0x10a8c4   pc=0x217269c
             [2]@2038 = 0x10b55c   pc=0x21730cc

That is ordinary C++ construction writing the base then the derived vtable.
**Nothing touches it again for the rest of the run.** The manager at 0x45f3964
is wiped at call 440,610 while the pool is alive and untouched, so "the pool is
torn down and takes its objects with it" is wrong. `DUMP1` confirms the pool
intact at exit, vtable 0x0010b55c and all fields sane.

## What the dumps do show

The pool's TLSF control at 0x45002e0 has **`fl_bitmap` = 0** -- not one free
block in any list -- while the pool's own accounting still reports 1,390,384
bytes on the field at +0x3c, and `ALLOCONLY` reports **301 of 589 allocations
failing** on this pool. The allocator and the pool disagree about whether
memory is available.

## Two probes that were being read as evidence, and are not

**1. `debug_watch_sink` cannot fire on a code address.** Every `setCount_*`
slot has reported `hits=0` for many runs, which has been read as "that code
never executes". It is not. `debug_watch_sink` is only ever fed by
`ppc_debug_watch` from the store-watch path in `ppc_runtime.h`, using the
synthetic `0xf00000xx` tags. No real PC is ever reported into it, so a slot
keyed on one can never fire regardless of what the code does. A note now sits
above those slots. PC-keyed capture has to go through `g_ppc_watch[]`, which is
emitted into the generated code and does work.

**2. `OVERLAP`'s "no overlap" is still on a dead branch** -- 0x215dc40 is the
large-count path (`newCount > 0x400`); the live path is 0x215dc7c.

## The path that does the damage, disassembled

    igObjectList::append          0x215dd08: bl 0x215dbfc
      igDataList::resizeAndSetCount 0x215dbfc  -- rounds capacity to a power of
                                                two, or to a 1024 multiple when
                                                newCount > 0x400
        resize                      0x215db1c  -- r4==0 frees; otherwise looks
                                                up a pool through
                                                lha r6,0xc(r12) / lwzx r27,r10,r0
          the move                  0x2184e1c  -- r3=pool r4=dest r5=bytes

`lha` was checked as a candidate for a wrong index, since it sign-extends and
its result indexes the pool table two instructions later. conquertron emits it
correctly: `(uint32_t)(int32_t)(int16_t)ppc_load_u16(...)`, with `lhz`
zero-extended for contrast. Not the bug.

## Next run

Three `g_ppc_watch` slots repointed onto that path, replacing questions already
answered (igArkCore::init runs; addWork hits=3 size=0x800; Module constructed):

    w0 -> 0x2184e1c   the move        r3=pool r4=dest r5=bytes
    w4 -> 0x215db1c   resize entry    r3=this r4=newCapacity r5=elemSize
    w7 -> 0x215dd08   append's call into resizeAndSetCount

That gives the destination and byte count of the write that lands on the
manager, which separates "writes outside its own buffer" from "handed a block
that is still live".
