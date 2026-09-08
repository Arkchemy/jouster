# XMLMERGE: n=0 — and the code says exactly why

Run of 2026-09-08, build `Sep  8 2026 22:12:47`.

```
XMLMERGE n=0 <none>
```

`igXmlNode::merge` is never called. It has three callers in the whole image, one
of which is itself; the other two are in **`igRegistry::read(igFile*)`**, and
the disassembly makes the condition unambiguous:

```
21c3aa0: bl  0x21e287c   ; igXmlDocument::read(igFile*)
21c3aa4: or. r31, r3, r3 ; the return value
21c3aa8: bne 0x21c3acc   ; NON-ZERO -> jump past the merge
21c3aac: mr  r3, r28
21c3ab0: bl  0x21e2870   ; rootElement() of the document just read
21c3ab8: lwz r3, 0xc(r30)
21c3abc: bl  0x21e2870   ; rootElement() of the registry's own document
21c3ac0: li  r5, 1
21c3ac8: bl  0x21e07f4   ; merge(registryRoot, fileRoot, 1)
```

**The merge is skipped when the XML read returns non-zero.** Since the merge
never runs, `igXmlDocument::read` is failing -- so `alchemy.xml` is opened, read
in full, parsed into a document, and then thrown away because the parse reports
an error.

That completes the chain from a config file to a blank screen, and every link is
now measured rather than assumed:

```
igXmlDocument::read(alchemy.xml) returns non-zero
  -> igRegistry::read skips the merge
  -> the registry keeps its compiled-in defaults
  -> startLevel stays "test", vramBSize stays a default
  -> streamContext::load closes bootstrap.bld to open level/test.bld
  -> that file does not exist; the open fails and never registers
  -> no archive is a registered storage device
  -> igFileContext::update has nothing to pump
  -> 131,072 of 198,695 bytes read, 1 of 35 blocks decompressed
  -> the scene is empty
  -> 1,341 frames presented, zero draws
```

## Next

Build `Sep  8 2026 22:28:31`, md5 `26357aa36600853225919553c6d51766`, is on the
Switch.

**XMLREAD** captures `igXmlDocument::read`'s return value at all three of its
exits, and hooks rapidxml's `parse_error_handler` to record the **message and
position of the first failure**. rapidxml's messages are static strings --
"expected <", "expected =", "unexpected end of data" -- so the message names the
failure precisely rather than merely confirming one happened.

Only the first error is kept: a later one would be a consequence of the parser
already being lost, and keeping the last would report that instead. The same
mistake as keeping only the last write to a corrupted word, which cost a run
earlier this week.

One thing to watch. The game's `setjmp` and `longjmp` are themselves recompiled
PowerPC, and a `longjmp` that restores guest registers cannot unwind the *host*
call stack the recompiled code actually runs on. If rapidxml's error path
depends on it -- which it does when built without exceptions -- that is a
plausible cause of a parse that fails on valid input. Worth suspecting only if
the handler is shown to run; the message will say.
