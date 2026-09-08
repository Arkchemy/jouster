# ARCHDRIVE: the context runs 1,363 times, the archive 8

Run of 2026-09-08, build `Sep  8 2026 20:00:02`.

```
ARCHDRIVE igFileContext::update=1363 igArchive::update=8 updateArchiveSystem=8
```

`igFileContext::update` runs **1,363 times** -- about once per presented frame,
matching the 1,313 buffer swaps -- while the archive's own update runs **8**.
The archive is not being pumped, and the first of the three outcomes written
down last time is the one that landed.

## The gate, from the disassembly

`igFileContext::update` walks its device list and calls each device's update
only if a predicate passes:

```
216e690: lwz   r10, 0x14(r11)   ; the device array
216e694: lwzx  r29, r10, r30    ; this device
216e69c: lwz   r12, 0x184(r11)  ; vtable+0x184
216e6a8: bctrl
216e6ac: cmpwi r3, 0
216e6b0: beq   0x216e6cc        ; zero -> SKIP this device entirely
216e6b8: lwz   r0, 0x13c(r6)    ; vtable+0x13c = update()
216e6c8: bctrl
```

Resolving the two slots against the retail symbol table:

```
+0x13c  update__Q2_4Core9igArchiveFQ2_4Core14igBlockingType
+0x184  getIsActive__Q2_4Core22igVirtualStorageDeviceFv
```

And `getIsActive` is two instructions:

```
216df60: lbz r3, 0x20(r3)
216df64: blr
```

A plain byte at `+0x20`. So the archive is in the device list and that flag is
reading **zero**, which is why it is skipped on all 1,363 walks. Its 8 updates
come from somewhere else -- direct calls during open and load, not the per-frame
pump.

There is also a gate at the top of the function, `this[0x10]` through
vtable+0xcc, which returns early for the whole walk; that one is evidently
passing, since the loop is running.

## Why this is a good place to be

The chain from symptom to suspect is now unbroken and every link is measured:

```
a byte at device+0x20 reads 0
  -> igFileContext::update skips the archive on every frame
  -> igArchive::updateArchiveSystem runs 8 times instead of ~1,300
  -> startNewTasks starts 1 block read of 35
  -> 131,072 of 198,695 bytes are read and the load stops
  -> the scene has no content
  -> the renderer presents 1,313 empty frames and draws nothing
```

Nothing in that chain is a graphics problem.

## Next

Build `Sep  8 2026 20:13:57`, md5 `8c9d946fff6865c2ea4968afa9ff2198`, is on the
Switch.

**ARCHDEV** records, per device pointer, how many walks saw it, how many times
it passed the gate, and the raw `+0x20` byte -- keyed by pointer because "some
device is inactive" is useless and "this device, at this address" is what can be
acted on. It also counts `igArchive::spinUp` and `spinDown`, since spinUp is the
obvious candidate for what sets that flag.

* **spinUp=0** -- the archive is never spun up, and the question is what should
  have called it
* **spinUp>0 and the flag still 0** -- either spinUp does not set `+0x20`, or
  `spinDown` follows and clears it
* **one device active and another not** -- the inactive one is identified by
  address and can be matched against the archive object directly
