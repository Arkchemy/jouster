# Probes

Probe call sites live *inside* `game/source/generated_*.c`, which
`regenerate.sh` overwrites wholesale. Anything kept there is destroyed by
the next regeneration — that is what happened to the eight probes behind
the 2026-08-27 log (see `docs/game-probe-reconstruction.md`).

So they live here as patches instead, and `apply.sh` re-applies them
after regenerating. Each patch should be a single `arkchemy_probe4(...)`
line, which prints named values rather than smuggling four numbers
through the allocation logger's parameters.
