# The manager is not over-released -- its block is handed out twice

Run of 2026-09-06, build 12:22:57, with the refcount ledger retargeted from
the pool at 0x4500274 onto the default frame manager at 0x45f3964.

## The ledger settles the refcount question: it is not an over-release

Eight entries, the complete Ref/Release history of the object (cap is 14, so
nothing was dropped):

    fn=0x215c464 Ref      lr=0x215e958  rc=0x800002
    fn=0x215e014 resize   lr=0x217b79c  rc=0x800001
    fn=0x215d194 Release  lr=0x217b7a4  rc=0x800002
    fn=0x215c464 Ref      lr=0x217b7a4  rc=0x800002
    fn=0x215c4cc assign   lr=0x217b7d0  rc=0x800001
    fn=0x215c464 Ref      lr=0x217b7d0  rc=0x800002
    fn=0x215d194 Release  lr=0x217b800  rc=0x800002
    fn=0x215c464 Ref      lr=0x217b800  rc=0x800002

The count oscillates between 1 and 2 and **never reaches 0**. The last value
written to +4 is 0x800001 at call 264,395. So the previous session's two
candidate explanations -- "over-released" and "never retained" -- are both
wrong. The refcounting is balanced.

## What actually wipes it

The watch on +0 (the vtable) fires 9 times, last at call **440,687**, writing
**0**, with `lr=0x215dd0c`. That return address is inside
`Core::igObjectList::append`, immediately after:

    215dd08: bl 0x215dbfc     ; igDataList::resizeAndSetCount
    215dd0c: lwz r10, 0x14(r29)

and `resizeAndSetCount` allocates at 0x215dc7c. So a list grows, the allocator
hands it a block, and initialising that block zeroes the manager's header --
vtable included, which is why every later virtual dispatch through
`context+0x18` lands on a null.

The reported `pc` for that write is 0x21565ac (`igCafeMutex::unlock`), which is
stale: the zeroing runs in a memset-style shim that never updates the PC, so
the last mutex unlock before it is what gets recorded. The link register is the
reliable field here.

## Corroboration that the address is recycled, not released

The watch on +4 caught a write of **0x45002e0** -- a pointer to the TLSF
control -- at call 204,640, *before* the manager was constructed there
(0x800001 written at call 261,952). So 0x45f3964 already held a different
object earlier in the run. The address is being recycled repeatedly.

`RELWINDOW` still records no releases around the wipe, which fits: nothing is
being released. The block is simply reissued while still live.

## What this points at

The manager is allocated from pool index 2, the pool object at 0x4500274
(`ALLOCPOOL managerPoolIndex=2 resolvedPool=0x4500274`). A pool-level reset
would free every block in it at once with no per-object release, leave the
refcounts untouched, and allow a later list allocation to be handed the same
memory -- which is exactly the observed pattern.

The `OVERLAP` probe reports `listGrowths=352 covering 0x45f3964: <no overlap>`,
which contradicts the wipe. That probe was noted last session as having been
placed on the large-count branch at 0x215dc40 while the live path is 0x215dc7c,
so its negative result should not be trusted until it is re-hooked.

## Next probe (queued, no rebuild)

    store1=0x4500274   # the pool's own vtable -- catches a teardown/reset
    store2=0x45f3964   # keep: timestamps the wipe for correlation
    dump1=0x4500274    # pool state at exit
    dump2=0x45002e0    # its TLSF control -- free-list state

If the pool is reset around call 440,6xx the first watch will show it, and the
fix is to give the default frame manager a lifetime that outlives that pool
(or stop the workers depending on it). If the pool is untouched, the allocator
is reissuing live memory and the bug is in the TLSF layer.
