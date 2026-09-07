# WRITELOG: the first zero comes from a job-queue worker thread

Run of 2026-09-07, build `Sep  7 2026 22:46:02`.

```
WRITELOG n=7
 [0 @204257 val=0x3ac8d4 pc=0x21effc0 lr=0x21f07a4]
 [1 @204257 val=0x3ac8d5 pc=0x21effc0 lr=0x21f07a4]
 [2 @204257 val=0x3ac8d7 pc=0x21effc0 lr=0x21f07a4]
 [3 @204257 val=0x3ac8d5 pc=0x21effc0 lr=0x21f07a4]
 [4 @204267 val=0x0      pc=0x21db584 lr=0x0]
 [5 @204393 val=0x0      pc=0x257d540 lr=0x21f07a4]
 [6 @204400 val=0x10000  pc=0x21f04f0 lr=0x217cc58]
```

Resolved against the retail symbol table:

```
0x21effc0  search_suitable_block      entries 0-3, the pc
0x21f07a4  tlsf_memalign+0x94         entries 0-3 and 5, the lr
0x21db584  jqWorkerThread             entry 4  <-- the first zero
0x257d540  sputc_limit (snprintf)     entry 5
0x21f04f0  tlsf_block_size            entry 6
0x217cc58  mallocInternal+0x54        entry 6, the lr
```

Entries 0 to 3 are the allocator legitimately writing the block's size and
flag bits: `0x3ac8d4` is the correct size, then the free and prev-free bits
settle. Entry 6 is the trailing size marker already identified, a consequence.

**Entry 4 is the corruption**, and the last write before it is a healthy
`0x3ac8d5`.

## What the pc field can and cannot say here

`g_ppc_current_pc` is a single `volatile uint32_t`, **not** thread-local, and
this run has `threads: created=2 started=2` with `w6(jqWorkerLoop) hits=2`.
With more than one thread running it names whatever function *any* of them
last entered, so it is not the writer.

Entry 5 shows that plainly: `pc` says `snprintf` while `lr` says
`tlsf_memalign+0x94`. No single thread is in both. That incoherence is the
signature of two threads interleaving -- but only of two threads running at
all, which is already known. **It is not evidence that two threads were inside
the allocator together**, and it should not be read as such.

## What this does establish

Something writes zero over a live TLSF block header at call 204,267, and it is
not the allocator, whose own writes at 204,257 are correct and whose replayed
sequence is correct on both architectures. The candidate that fits every fact
is a race: the single-threaded replay cannot reproduce a concurrent
interleaving no matter how faithful the call list is.

Retail supports this indirectly -- its identically sized pool stays whole under
Cemu, so whatever locking the real game has works.

## Next

Build `Sep  7 2026 23:00:39`, md5 `030e7fa38baf7c5253ebea94cb32aa49`, is on the
Switch.

**ALLOCRACE** counts it rather than inferring it: a depth counter incremented
on entry to each `tlsf_*` function and decremented on exit, with the peak, the
overlap count and the distinct thread contexts reported. `reallocCommon` takes
an `igScopeLock` on the pool's mutex at `0x216f900`, and the mutex shim uses
real pthread mutexes keyed by guest address, so a peak above 1 means either
something reaches the allocator without that lock or the lock is not doing its
job.

The counter is deliberately not atomic, which means it can only ever
**undercount**. A peak above 1 is therefore trustworthy; a peak of exactly 1 is
weak evidence and must not be reported as proof that locking held.

**WRITELOG** also now records the `PpcContext` pointer per entry, which unlike
`pc` really is per-thread, so entry 4's writer can be attributed to a specific
thread rather than guessed at.
