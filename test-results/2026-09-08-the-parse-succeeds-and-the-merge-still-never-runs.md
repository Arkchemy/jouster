# XMLREAD: the parse succeeds — which contradicts the previous conclusion

Run of 2026-09-08, build `Sep  8 2026 22:28:31`.

```
XMLREAD calls=1 ret=0x0 | parseErrors=0 first="" at=0x0
```

`igXmlDocument::read` was called once and returned **0**, and rapidxml's
`parse_error_handler` **never ran**. The parse of `alchemy.xml` succeeds.

That contradicts what I wrote last time. The reasoning was:

```
21c3aa4: or. r31, r3, r3
21c3aa8: bne 0x21c3acc   ; non-zero -> skip the merge
```

and since `XMLMERGE` reported `n=0`, I concluded the read must be failing. It
is not. With `ret=0` the branch is **not** taken and the merge should run.

Both observations cannot describe the same call, so one of my assumptions is
wrong -- and the assumption was that the read which returned 0 is the one inside
`igRegistry::read`.

Also worth retracting: the `setjmp`/`longjmp` suspicion. It was flagged as
worth suspecting only if the error handler ran, and it did not. The concern
about recompiled `longjmp` and the host stack may still be true in general, but
it has nothing to do with this.

## What was verified before theorising

* the `ark_merge` hook is present in `generated_0165.c` and the `XMLMERGE`
  string is in the built binary, so `n=0` is a real measurement and not a
  missing probe
* `igRegistry::read` exists in the image at `0x21c3a28` and has two callers,
  `0x2148004` and `0x2148140`

What is **not** measured is whether `igRegistry::read` ever runs, and which
caller invoked the `igXmlDocument::read` that succeeded.

## Next

Build `Sep  8 2026 23:29:29`, md5 `7ee72e204ec323ab525da6f748403ba9`, is on the
Switch. **XMLWHO** counts `igRegistry::read` with its return value, and records
the `lr` of every caller of `igXmlDocument::read`.

* **`igRegistry::read` calls=0** -- the registry never loads the file at all.
  The successful read came from somewhere else entirely, the merge was never
  reachable, and the question moves to `0x2148004` / `0x2148140` and why
  neither runs.
* **calls>0 with the reader lr inside it** -- the read succeeded *and* the merge
  was skipped anyway, which would mean the branch is not behaving as the
  disassembly reads, and that is a recompilation bug worth isolating on the
  host harness.

The wider chain is unchanged and still measured end to end: whatever the reason,
the registry keeps `startLevel = "test"`, `level/test.bld` does not exist, no
archive stays registered, and the renderer presents empty frames.
