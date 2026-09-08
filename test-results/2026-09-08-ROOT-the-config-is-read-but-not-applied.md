# ARCHNAME: the boot archive is closed to load a level that does not exist

Run of 2026-09-08, build `Sep  8 2026 21:00:39`.

```
ARCHNAME n=2
 [this=0x829a610 "permanent/bootstrap.bld" closedBy=0x2000aac]
 [this=0x9740614 "level/test.bld"          closedBy=0x2000aac]
```

The archive that gets registered and then removed **is** the boot archive. And
the second one the game asks for is `level/test.bld`.

**That file does not exist.** The disc's `content/level/` holds `Title.bld`,
`_AllMinions.bld` and 60-odd `Challenge_Level_*.bld`. There is no `test.bld`
anywhere in the game data, and the FS log confirms the open fails:

```
/vol/content/level/test.bld -> .../content/level/test.bld mode="r" (NOT FOUND)
```

## Why it asks for that

`tfbGame::streamContext::load` is a reload: close whatever archive is open,
then open the new one.

```
2000a98: lwz   r8, 0x28(r18)   ; is an archive already open?
2000aa0: beq   0x2000b88       ;   no -> skip the close
2000aa8: bl    0x2169738       ; close(previous)
```

So loading a level legitimately closes the boot archive. The bug is not the
close -- it is the level name.

Three strings sit adjacent in `.rodata`:

```
0x1000c6fc  'test'
0x1000c704  'level/'
0x1000c718  'startLevel'
```

A config key, its default, and the path prefix. The game reads `startLevel`
and, absent a value, falls back to a development level that never shipped.

## The config is read, and ignored

`content/alchemy.xml` **does** ship, and it says exactly what it should:

```xml
<Tfb  startLevel = "Title"  debug = "false" />
```

`Title.bld` exists. And the FS log shows the file opening **successfully, three
times, at its full 542 bytes**:

```
/vol/content/alchemy.xml -> sdmc:/switch/Jouster/content/alchemy.xml (542 bytes)
content:/alchemy.xml     -> ... (542 bytes)
alchemy.xml              -> ... (542 bytes)
```

So this is not a missing file or a path problem. **The config is read and its
values are not being applied.**

That also retires a question left open days ago. The same file sets
`vramBSize = "419430400"`, 400 MB, while the VRAM request actually seen was
367,001,600 -- a default, not the configured value. One cause, two symptoms.

## The chain, complete

```
alchemy.xml is read but its values do not take effect
  -> startLevel falls back to the built-in default, "test"
  -> streamContext::load("level/test.bld") closes bootstrap.bld to load it
  -> that file does not exist, so the open fails and never registers
  -> no archive is a registered storage device
  -> igFileContext::update has nothing to pump, archive update runs 8 times
  -> 131,072 of 198,695 bytes read, 1 of 35 blocks decompressed
  -> the scene is empty
  -> the renderer presents 1,341 frames and draws nothing
```

Every link is measured. None of it is a graphics problem, and the shader wall
is not the blocker.

## Next

Build `Sep  8 2026 21:16:48`, md5 `0e90071e9da6acaf9e6adaeaee33b12f`, is on the
Switch.

**XMLCFG** counts both `igXmlDocument::read` overloads and records every
`igXmlNode::getAttribute` **by name**, with how often each was asked for and how
often a value came back. The name matters: a bare count of failed lookups would
not say whether `startLevel` was ever asked for.

* **`startLevel` asked, got=0** -- the parse produced no such attribute, and
  the fault is inside the rapidxml parse of a 542-byte file, which is small
  enough to reproduce on the host harness
* **`startLevel` never asked** -- the config is read into a document nobody
  queries, and the search moves to whatever should consume it
* **`read` counts zero** -- the file is opened by something that never hands it
  to the XML parser at all
