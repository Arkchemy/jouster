# XMLVAL: `startLevel` really is "test" — the config is parsed but not merged

Run of 2026-09-08, build `Sep  8 2026 21:58:48`.

```
XMLVAL read(file)=1 read(path)=1 total=308 distinct=38 | values that came back:
 ["tripleBuffer"="true"                a=2 g=1 @14]
 ["name"="_scheduler"                  a=5 g=5 @32]
 ["refname"="frameTime"                a=36 g=36 @230]
 ["type"="igFrameTime"                 a=36 g=36 @231]
 ["ref"="this.graphicsUpdater"         a=70 g=34 @232]
 ["root"="true"                        a=30 g=1 @234]
 ["maxShaderParametersAttr"="32"       a=1 g=1 @238]
 ["startLevel"="test"                  a=1 g=1 @300]
```

`getAttribute("startLevel")` returns **`"test"`**. Not null, not garbage -- the
value genuinely present in the document being queried. And `alchemy.xml` says
`"Title"`.

So the document the game reads its configuration from is **not** the file that
was loaded.

## The evidence for a defaults document

`maxShaderParametersAttr` comes back `"32"` and **does not appear anywhere in
`alchemy.xml`**. Something is supplying values the file never contained, which
means a separate defaults document exists and is what every lookup is hitting.

`tripleBuffer="true"` is consistent with either, since the file and the default
presumably agree. The XML machinery is working correctly -- values parse,
strings come back intact, and the object-loading attributes at `@20..@238`
(`name`, `refname`, `type`, `ref`) all resolve properly with 36 of 36 hits. The
parser is fine.

## What this reframes

Three earlier readings were each partly wrong, and it is worth being explicit:

* "the config never reaches the game" -- wrong, the file opens at its full 542
  bytes three times
* "the parse silently drops startLevel" -- wrong, it is asked for and answered
* "a non-null return means the right value" -- wrong, and the reason the last
  probe could not see this

What is actually true: **the file is read, and never merged into the document
the game queries.**

The sequencing oddity found last run fits the same explanation rather than
being separate. `tripleBuffer` failed at call 5 and succeeded at call 14 because
the defaults document was not yet built at call 5, not because alchemy.xml
arrived in between.

## Next

Build `Sep  8 2026 22:12:47`, md5 `97c6c0b91814ca4d4e04ee8b05129695`, is on the
Switch.

`igXmlNode::merge` (`0x21e07f4`, 3,768 bytes) is how one document is folded into
another. **XMLMERGE** records every call with its destination, source, flags and
return value.

* **n=0** -- alchemy.xml is parsed and then simply never merged, so the defaults
  stand. The question becomes what should have called merge, and why it does
  not.
* **n>0 with a failing return** -- the merge is attempted and rejects the
  document, and a 542-byte file driven through `conquertron/hosttest` will
  reproduce it on this machine in milliseconds.
* **n>0 succeeding** -- the merge works and something re-reads the defaults
  afterwards, which would be a third and more awkward possibility.

Worth stating: this single failure accounts for the level name, for `vramBSize`
being ignored in favour of a 350 MB default, and for the job-queue heap size.
One cause, several symptoms that have been investigated separately for days.
