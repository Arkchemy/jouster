# Reading Cemu's guest memory without a debugger

Established 2026-09-03. This replaced eleven rounds of inference with one
measurement, and it is reusable for any future question of the form "what
does the real game have here".

## The technique

Cemu maps the whole Wii U address space into its own process at a fixed
base, so guest memory can be read straight out of `/proc/<pid>/mem` while
the game runs at full JIT speed. No gdb, no breakpoints, no stopping the
target, no `--force-interpreter`.

The base moves with ASLR every launch, so derive it rather than hardcode it.
Anchor on a string whose guest address is known from the recompiled code --
`lis r26,0x1005; addi r26,r26,0x1308` puts `"ram:/alchemy.xml"` at guest
`0x10051308`:

```
scan /proc/<pid>/maps for readable regions
find b"ram:/alchemy.xml\0"
base = host_address_of_string - 0x10051308
host = base + guest          # guest words are BIG-ENDIAN
```

A wrong base cannot pass silently: the anchor either matches or it does not.
Tooling lives in `scratchpad/peek.py` and `scratchpad/watchjq.py`.

## Why the gdb route is a trap

All three of these cost a cycle each, and all three were already in the
2026-08-29 notes:

- the stub implements no detach, so Cemu must be restarted per gdb session
- breakpoints only fire under `--force-interpreter`
- gdb defaults to async, so `continue` returns immediately and every
  following command fails with "Cannot execute this command while the target
  is running". `maint set target-async off` fixes it.

And even with all of that right, the interpreter did not reach the job queue
in 25 minutes. Under JIT with `/proc/<pid>/mem`, the same information took 90
seconds.

## The measurement that mattered

Guest addresses derived from our own `lis`+offset pairs:

| guest | meaning |
|---|---|
| `0x10136b20` | flag A -- the work counter |
| `0x100cd5e4` | jqKeepWorkersAwakeCount |
| `0x100cd5e8` | jqSleepingWorkersCount |
| `0x100cd602` | jqStopSignal |
| `0x10130ccc` | archive owner |
| `0x10130cd0` | archive task list (`+8` = count) |
| `0x10130cd4` | block-task list (`+8` = count) |

### Idle, after boot -- IDENTICAL on both

```
A=0  C=0  sleeping=2  stopSignal=0
```

930 sweeps over 20 s on the real game, nothing moved. **Workers asleep with
A and C at zero is the normal resting state, not a fault.** An entire
afternoon was spent treating it as pathology.

The only difference at rest is the lists: real game `arch_list_count = 0`
and `updq_list_count = 0` -- drained. Ours holds 1 in each, forever.

### During the archive load -- where they diverge

Real game, the whole load in about one second:

```
S_sleeping      0 -> 2 -> 1      a worker WAKES
arch_list_count 0 -> 1 -> 0      fills and drains
updq_list_count 0 -> 1,6,3,4,5   churns
A_flagA         0 -> 4,1,3,2     live work counter
```

Ours: `A` never leaves 0, no worker ever wakes, `updq_list_count` sticks at
1 for 3.98 million calls.

## The conclusion

**Flag A is a live work counter, and nothing in our build ever increments
it.** That is the defect. Everything else measured today -- the sleeping
workers, the unexecuted batch, the polling `_decompressJobFlag`, the parked
`blockUntilComplete` -- is correct behaviour downstream of that one missing
increment.

Every static search for A's writer came up empty, including both `lis`
encodings (`0x1013 + 0x6b20` and `0x1014 - 0x94e0`). That absence is now
informative rather than puzzling: A is most likely a **field reached through
a pointer**, not a global addressed by `lis`, which no global-address search
would ever find. The next step is to find which structure it lives in, and
Cemu can answer that too -- watch the address and see which code writes it.
