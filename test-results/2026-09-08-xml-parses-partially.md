# XMLCFG: the document is read, and lookups mostly come back empty

Run of 2026-09-08, build `Sep  8 2026 21:16:48`.

```
XMLCFG read(file)=1 read(path)=1 getAttribute total=308 distinct=10
 ["stringPoolStatisticsLev"  asked=1 got=0]
 ["jobqueueProfileEventCou"  asked=1 got=0]
 ["jobqueueBatchDataHeapSi"  asked=1 got=0]
 ["jobqueueProcessorMask"    asked=1 got=0]
 ["tripleBuffer"             asked=2 got=1]
 ["dynamicVertexBufferSize"  asked=2 got=0]
 ["showFrameRate"            asked=1 got=0]
 ["commandLineFrameRate"     asked=1 got=0]
 ["softwareBlending"         asked=1 got=0]
 ["texturePoolSize"          asked=1 got=0]
```

`igXmlDocument::read` runs, once per overload, so the file does reach the
parser. 308 attribute lookups happen.

## The table hit its cap, so this is partial

Ten distinct names out of 308 lookups, and the table holds exactly ten. It
filled and stopped. `startLevel` lives in `<Tfb>`, the **last** element of the
file, so it is almost certainly among the names that were dropped -- which
means this run cannot say whether it was ever asked for. That was the one
question the probe was built to answer.

Same mistake in a new place: a fixed-size table recording on arrival order,
which has now cost a run three times this week.

## What can be read from it anyway

Four of these names are genuinely in `alchemy.xml` -- `jobqueueBatchDataHeapSize`
in `<Core>`, and `softwareBlending`, `dynamicVertexBufferSize` and
`tripleBuffer` in `<Gfx>`. Only `tripleBuffer` ever came back.

The shape of that one is the interesting part: **asked twice, got once.** Every
other name here was asked once and got nothing. The obvious reading is that a
lookup made *before* the document is parsed returns nothing and one made after
succeeds -- and only `tripleBuffer` was asked on both sides of the parse.

`dynamicVertexBufferSize` was also asked twice and got nothing either time,
which does not fit that reading, so it is a hypothesis rather than a finding.
Ordering is what separates them, and the first version did not record it.

## Next

Build `Sep  8 2026 21:41:49`, md5 `e41f71c6cdd966e2ce81a7b2b8b864d9`, is on the
Switch. The table is widened from 10 to 48 entries -- comfortably more than the
distinct keys in a 542-byte file -- and each entry now carries the **first and
last call index** at which that name was asked.

That answers both open questions at once:

* whether `startLevel` is asked for at all, and whether it comes back
* whether the failures cluster before some point and the successes after it,
  which would make this a sequencing problem -- config consumed before it is
  loaded -- rather than a parser problem

If instead the lookups are spread throughout and nearly all fail, the parse
itself is producing an almost empty document, and a 542-byte file is small
enough to drive through `conquertron/hosttest` and bisect on this machine.
