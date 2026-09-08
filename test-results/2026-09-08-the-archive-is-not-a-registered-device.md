# ARCHSELF: the archive is not in the file context's device list

Run of 2026-09-08, build `Sep  8 2026 20:28:10`.

```
ARCHSELF archive this=0x829a610 vtable=0x109ed8

ARCHDEV spinUp=2 spinDown=1 n=8
 [dev=0x4530218 vt=0x10b124 seen=1341 active=1341 f20=0x4]
 [dev=0x452e8a0 vt=0x1090f4 seen=1341 active=0    f20=0x4]
 [dev=0x4552e3c vt=0x10a444 seen=1341 active=1341 f20=0x1]
 [dev=0x4552e10 vt=0x10a444 seen=1341 active=1341 f20=0x1]
 [dev=0x4552de4 vt=0x10a444 seen=1341 active=1341 f20=0x1]
 [dev=0x452cf28 vt=0x1090f4 seen=1341 active=1341 f20=0x4]
 [dev=0x452b5b0 vt=0x1090f4 seen=1341 active=0    f20=0x4]
 [dev=0x4529c38 vt=0x1090f4 ...]
```

`0x829a610` appears nowhere in that list, and its vtable differs from all three
present. Mapping the vtables through `find_synth_addr`:

```
0x109ed8 -> 0x10052b58  __vtbl__Q2_4Core9igArchive               the archive
0x10b124 -> 0x10053da4  __vtbl__Q2_4Core21igMemoryStorageDevice
0x1090f4 -> 0x10051d74  __vtbl__Q2_4Core19igCafeStorageDevice    x4
0x10a444 -> 0x100530c4  __vtbl__Q2_4Core22igVirtualStorageDevice x3
```

**The archive was never registered as a storage device.** So the second of the
two possibilities is the one that landed, and the earlier guess -- that it was
present and gated by its `+0x20` flag -- is dead. Worth noting the split that
misled the last probe now makes sense: the two inactive devices are
`igCafeStorageDevice`, which reads `+0x2c`, not the `+0x20` the probe recorded.

## Where registration should happen

`igFileContext::addStorageDevice` (`0x216ce34`) has exactly **one** caller in
the entire image, at `0x21699e8`, inside `igArchive::open`:

```
21699e0: lwz r3, -0x2a80(r28)   ; the file context, from a global
21699e4: mr  r4, r29            ; the archive
21699e8: bl  0x216ce34          ; addStorageDevice(context, archive)
21699ec: mr  r31, r3            ; result
```

Nothing guards that call at the point it is made. So the archive is either
never reaching it, or is being added and then removed -- and `igArchive::open`
also calls `removeStorageDevice`, so removal is a live possibility rather than
speculation.

`spinUp=2 spinDown=1` says the archive does spin up, twice.

## Next

Build `Sep  8 2026 20:44:43`, md5 `8ebb460505977b7deaffc1d0c3924500`, is on the
Switch.

**DEVLOG** records every `addStorageDevice` and `removeStorageDevice` in order
with the device and its vtable, plus counts for `igArchive::open`,
`igArchive::close`, and specifically whether `open` reached its registration
call at all. Logged with the device rather than as bare counts, because with
eight devices in play "an add happened" says nothing.

Three readings:

* **no ADD for `0x829a610`, `reachedRegister=0`** -- `open` returns before
  registering, and the early exit above `0x21699e8` is where to look
* **an ADD followed by a REM for it** -- registration works and something
  removes it again, and the call count says when
* **an ADD and no REM, yet still absent** -- `addStorageDevice` accepted it
  without actually inserting it into the list the update walks

Nothing here is a graphics problem, and the measured chain from this to the
blank screen is unchanged.
