# There is a size threshold

Build `Sep 18 2026 11:21:11`, buffer size dialled from the card.

    log flush interval: 1 line(s), buffer 4096 bytes, setvbuf buffer
    OK  build=Sep_18_2026_11-21-11 flush=1 frames=14400 draws=12785 modules=7

**4096 boots.** `setvbuf` is called, the buffer is real and in use, and the run
is completely healthy — 12,785 draws against 12,807 for the previous build.

So calling `setvbuf` is not the trigger. The size is.

    ~1KB   newlib default   boots
    4KB    setvbuf          boots
    32KB   setvbuf          does not boot

## Why this matters more than it looks

Every note for three days said "buffering the log stops the game booting". That
was never the mechanism, and the phrasing hid how narrow the real effect is.
The stream is buffered in every configuration, including the working ones. What
distinguishes them is a single number between 4,096 and 32,768.

A threshold that tight, in a program whose only other notable memory fact is a
one-gigabyte static arena, points somewhere specific: **the host heap is close
enough to its limit that tens of kilobytes matter.** The buffer is `malloc`'d,
and a `malloc` that succeeds can still take memory a later guest allocation
needs. That is consistent with the boot diverging early and with
`RSALLOC calls=0` in the broken runs — the guest never reached pool
allocation.

It is also consistent with the BSS results that were read as exonerating
memory. 256KB and 32KB of BSS both broke it; so does 32KB of heap. Those were
never three different causes. They were the same one, and the reason "the BSS
hypothesis is dead" looked right was that both values tested sat on the same
side of a threshold nobody knew was there.

## The sweep

    0       newlib default   boots
    4096    boots
    16384   set now
    32768   does not boot

Bisecting. If the cliff is sharp and lands somewhere unremarkable, memory
pressure is the answer and the fix is to stop spending host heap rather than to
stop buffering. If it is gradual, or the boundary sits on something meaningful
like a page or a heap-bin size, that says something else.

## What it is worth

The log is 39% of every run and the largest single cost. If a 4KB buffer is
safe, most of that is recoverable immediately: fewer, larger writes with the
flush interval raised, at a buffer size the run tolerates. The reason not to
reach for that yet is that the threshold is unknown and 4,096 has been tested
exactly once.
