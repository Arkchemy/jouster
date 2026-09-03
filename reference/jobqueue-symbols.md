# Job-queue globals, named

Recovered 2026-09-03 from the Imaginators symbolized ELF
(`nova-cafe-fin.elf`, 1,021,582 symbols) and mapped onto our stripped
2011-lineage target by structure.

## Imaginators (2016), from the symbol table

| address | size | symbol |
|---|---|---|
| `10550008` | 8 | `jqMainThreadID__4Core` |
| `10550028` | 4 | `jqWorkersMask__4Core` |
| `10550030` | 4 | **`jqKeepWorkersAwakeCount__4Core`** |
| `10550034` | 4 | **`jqSleepingWorkersCount__4Core`** |
| `10550038` | 4 | `jqWorkerInitFn__4Core` |
| `1055003c` | 4 | `jqWorkerDeinitFn__4Core` |
| `10550050` | 1 | `jqInitialized__4Core` |
| `10550051` | 1 | `jqStarted__4Core` |
| `10550052` | 1 | **`jqStopSignal__4Core`** |
| `105c7880` | 384 | `jqPool__4Core` |
| `105c7a00` | 6192 | `jqWorkers__4Core` |
| `105c923c` | 36 | `jqAwakeWorker__4Core` |

## Our target (2011 lineage), `Core::jqWorkerSleep` at 0x21da10c

```
L_21da144:  lwarx/stwcx. on [14248]      ++ before the wait, -- after
            OSWaitEventWithTimeout(evt, 0, 100000000 ticks = 1.61 s)
L_21da17c:  A = [445456]      (word, .bss)   bne -> leave the loop
            B = byte[3524]    (byte, .data)  bne -> leave the loop
            C = [14244]       (word, .data)  beq -> sleep again
L_21da1a0:  OSYieldThread; lwsync; return !byte[3524]
```

## The mapping, and why it holds

| ours | name | evidence |
|---|---|---|
| `[14248]` | `jqSleepingWorkersCount` | incremented immediately before the wait and decremented immediately after -- the definition of a sleeping-worker count |
| `[14244]` = C | `jqKeepWorkersAwakeCount` | **exactly 4 bytes below the sleeping count, and Imaginators has these two adjacent in that same order** (`10550030` then `10550034`) |
| `byte[3524]` = B | `jqStopSignal` | it is a byte, and the function's return value is `!B` -- "keep looping unless stopped" |
| `[445456]` = A | pending work (unconfirmed) | in `.bss`, checked first, non-zero means leave the loop |

The adjacency of the two counters is the load-bearing evidence. It is a
structural match, not a guess from a name.

## What this says about the stall

A worker sleeps while **all three are zero**. Measured across a whole run:
285 timed wakeups, and it never leaves the loop. So none of the three is
ever set.

`jqStopSignal` being zero is correct -- nothing is shutting down.
`jqKeepWorkersAwakeCount` being zero means nothing ever asked the workers to
stay awake. That leaves A as the only thing that could have woken them.

Imaginators has **`jqAlertWorkers__4CoreFUi`** and
**`jqEnableWorkers__4CoreFUi`** -- an explicit producer-side "wake the
workers for this mask" call. **Our 2011 build has neither**; its whole jq
inventory is:

```
jqAddBatchToQueue  jqAttachQueueToWorkers  jqDetachQueueFromWorkers
jqFlush  jqPopNextBatch  jqProfGetThread  jqStart  _jqStart  _jqStop
jqTempWorkerLoopOnce  jqWorkerLoop  jqWorkerSleep  jqWorkerThread
```

So in this build the wake is not an explicit alert; it is the timed poll plus
whatever sets A. Which is exactly what the armed store watch on `445456`
will answer.

## Caveat

Imaginators is 2016 Alchemy; the target is the 2011 lineage. Names and
ordering are a strong guide, not ground truth. The two-counter adjacency is
what makes this particular mapping safe; do not extend it to offsets without
the same kind of structural check.

## Recipe

The ELF is at
`~/Documents/Dumpling/Games/Skylanders Imaginators/content/nova-cafe-fin.elf`.

```
readelf -sW <elf> | awk 'NF>=8 {print $2, $3, $4, $8}' > symbols.txt
```

Half a second, 1,021,582 lines, then grep it. No PowerPC disassembler is
installed, so this gives names and addresses but not code; conquertron's
`src/disassembler.cpp` is the only decoder on this machine.
