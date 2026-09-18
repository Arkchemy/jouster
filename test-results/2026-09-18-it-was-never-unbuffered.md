# It was never unbuffered

Build `Sep 18 2026 11:08:10`.

    LOGTHREAD main=30294 (137153ms) guest=305 (1532ms)

**99% of the log cost is the main thread.** The guest thread makes 305 calls
and spends 1,532ms in them across a six-minute run — about 1.1%. Blocking a
thread for one and a half seconds cannot account for the boot difference, so
the "the guest thread needs the fflush to deschedule it" lead is dead. That is
the fifth explanation for this phenomenon killed by a measurement that
addressed it directly.

## And the framing was wrong the whole time

Chasing that lead meant re-reading what the configurations actually do, and the
working one does not do what its own log line claims.

```c
if (g_log && g_log_flush_lines != 1u) {
    g_log_buf = malloc(ARKCHEMY_LOG_BUF_SIZE);
    if (g_log_buf) setvbuf(g_log, g_log_buf, _IOFBF, ARKCHEMY_LOG_BUF_SIZE);
}
```

Interval 1 **skips `setvbuf`**. Skipping `setvbuf` does not make a stream
unbuffered — it leaves newlib's own default buffering in place, roughly a
kilobyte, allocated by stdio on first write. The stream has not been unbuffered
in a single run of this project.

The log line said `stream UNBUFFERED` for three days. I wrote that label, and
every note since has reasoned from the word.

So the comparison was never buffered against unbuffered:

    newlib default, ~1KB   ->  boots, 12,807 draws
    32KB via setvbuf       ->  does not boot, 0 draws

**A working buffer and a broken buffer. The variable is size, not existence.**

That also explains the thing that never sat right: with a flush after every
line, both configurations issue one `write()` per line. The syscall pattern is
near-identical, which is why "buffering changes the fs access pattern" always
felt thin. It is not the pattern. Something about a 32KB buffer specifically.

## What is being measured next

Build `Sep 18 2026 11:21:11`, with the buffer size on the card as
`log-buf-bytes.txt`, independent of the flush interval. One binary sweeps the
whole range.

  * `0` — no `setvbuf`, newlib's own. The known-good.
  * `4096` — set now.

If 4096 boots and 32768 does not, there is a threshold and it can be bisected
in a handful of runs. If 4096 fails, then calling `setvbuf` at all is the
trigger regardless of size, which is a different and stranger result.

Either way the honest position is that five days of notes about "buffering"
were describing something narrower than they claimed, and the label that caused
it was mine.
