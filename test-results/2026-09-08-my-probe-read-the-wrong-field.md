# ARCHDEV: same flag, opposite outcomes — the probe read a field its subject never consults

Run of 2026-09-08, build `Sep  8 2026 20:13:57`.

```
ARCHDEV spinUp=2 spinDown=1 n=8
 [dev=0x4530218 seen=1321 active=1321 flag=0x4]
 [dev=0x452e8a0 seen=1321 active=0    flag=0x4]
 [dev=0x4552e3c seen=1321 active=1321 flag=0x1]
 [dev=0x4552e10 seen=1321 active=1321 flag=0x1]
 [dev=0x4552de4 seen=1321 active=1321 flag=0x1]
 [dev=0x452cf28 seen=1321 active=1321 flag=0x4]
 [dev=0x452b5b0 seen=1321 active=0    flag=0x4]
 [dev=0x4529c38 seen=1321 active=0    flag=0x4]
```

Three devices with `flag=0x4` pass the gate and three with `flag=0x4` fail it.
That is not a contradiction in the game -- it is my probe reading a field that
half of these objects never look at.

## Correcting the previous note

Last time I wrote that `getIsActive` is "two instructions, a plain byte at
`+0x20`", and concluded the archive is in the device list with that byte
reading zero. **That was wrong**, and the error was assuming one implementation
of a virtual function.

`getIsActive` is virtual and there are two implementations that read **different
fields**:

```
igVirtualStorageDevice::getIsActive     lbz r3, 0x20(r3)          byte at +0x20
igPhysicalStorageDevice::getIsActive    lwz r12, 0x2c(r3)         word at +0x2c
                                        addic/subfe -> (x != 0)
```

Which vtables use which, from `.rodata`:

```
igVirtualStorageDevice::getIsActive  in  igArchive, igVirtualStorageDevice,
                                         tfbSound::tfbArchive, tfbStreamContainer x2
igPhysicalStorageDevice::getIsActive in  igCafeStorageDevice, igPhysicalStorageDevice,
                                         igMemoryStorageDevice
```

So the failing devices are physical ones reading `+0x2c`, and the `flag=0x4` my
probe recorded for them is a byte they never consult. Meanwhile every device
whose `+0x20` is non-zero passed -- which is what the virtual implementation
would do.

**And that undermines the earlier conclusion outright.** `igArchive` uses the
virtual implementation, so if the archive were in this list its `+0x20` would
have to be zero to be skipped -- and no entry here is both skipped and using
that implementation. The likelier reading now is that **the archive is not in
this device list at all**, which is a different bug from failing a gate.

`spinUp=2 spinDown=1` is also worth noting: the archive does spin up, twice,
and spins down once.

## Next

Build `Sep  8 2026 20:28:10`, md5 `cff7ebc5ab403b3d9d65de1883d42604`, is on the
Switch.

**ARCHDEV** now records each device's **vtable pointer**, which names its class
and therefore which `getIsActive` it runs -- the thing that would have prevented
this mistake. **ARCHSELF** records the archive's own `this` and vtable, captured
where `igArchive::update` runs.

Matching one against the other answers it directly:

* **the archive's `this` is in the device list** -- then it is being gated, and
  its `+0x20` is the field to chase after all
* **it is absent** -- the archive was never registered as a storage device with
  the file context, and the question becomes what should have registered it and
  why the eight direct calls to its update happen anyway

The wider chain is unchanged and still measured: whatever the reason, the
archive is pumped 8 times against the context's 1,363, one block of 35 is read,
and the renderer is faithfully presenting an empty scene.
