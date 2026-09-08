# XMLCFG widened: startLevel *is* found, and the game still loads test.bld

Run of 2026-09-08, build `Sep  8 2026 21:41:49`.

```
XMLCFG read(file)=1 read(path)=1 getAttribute total=308 distinct=38
 ["stringPoolStatisticsLev" a=1 g=0 @1..1]     ["name"      a=5  g=5  @20..32]
 ["jobqueueBatchDataHeapSi" a=1 g=0 @3..3]     ["refname"   a=36 g=36 @21..230]
 ["tripleBuffer"            a=2 g=1 @5..14]    ["type"      a=36 g=36 @22..231]
 ["dynamicVertexBufferSize" a=2 g=0 @6..15]    ["ref"       a=70 g=34 @23..232]
 ["softwareBlending"        a=1 g=0 @9..9]     ["base"      a=36 g=0  @24..233]
 ...                                           ["startLevel" a=1 g=1 @300..300]
```

**`startLevel` was asked for and it came back.** So the parse is not silently
dropping it, and the "config never reaches the game" reading from the last note
is wrong as stated.

The ordering also settles the earlier hypothesis. `tripleBuffer` was asked at
call 5 and failed, then again at call 14 and succeeded -- so the `<Core>` and
`<Gfx>` keys queried at `@1..@19` are being read **before the document has
content**, and only the one re-queried later succeeds. That is a real
sequencing problem, and it explains `vramBSize` being ignored in favour of a
default, which is the VRAM number chased days ago.

But it does **not** explain the level. `startLevel` is queried at `@300`, well
after the igz object loading at `@20..@299`, and it returns non-null.

## What that leaves

The run still opens `level/test.bld`, and still fails:

```
ARCHNAME [this=0x829a610 "permanent/bootstrap.bld"] [this=0x9740614 "level/test.bld"]
/vol/content/level/test.bld -> ... (NOT FOUND)
ARCHPUMP updateArchiveSystem=8 ... startBlockRead=1
GX2DRAW [GX2DrawEx x0]
```

So a non-null return is not the same as the right return. Either the value the
parser produced is not `Title`, or it is and something else supplies the level
name.

I recorded only whether the pointer was non-null, which cannot distinguish
those -- the same shape of mistake as recording `+0x20` for devices that read
`+0x2c`, and as the ten-entry cap the run before. A probe that answers "did
something come back" when the question is "what came back" is not a probe.

## Next

Build `Sep  8 2026 21:58:48`, md5 `8a160e81e32bb3670786bef843f4f8f2`, is on the
Switch. **XMLVAL** reads the returned pointer as a guest C string and reports
the actual value for every lookup that succeeded, printing only those -- a list
of 38 failures says less than the handful of values that came back.

* **`startLevel` = `"Title"`** -- the config is correct and something downstream
  ignores it, so the search moves to whoever builds `level/%s.bld`
* **`startLevel` = something else** -- the parser is returning a wrong string
  from a 542-byte file, which is small enough to reproduce and bisect on the
  host harness rather than on hardware

The sequencing problem at `@1..@19` is real and worth fixing regardless, since
it is why the VRAM size and the job-queue heap size are defaults.
