# Project schedule

A Gantt chart of Arkchemy, past and planned. Dates before 2026-09-08 are what
actually happened, taken from the project log and commit history; everything
after is planned and deliberately coarse — a reverse-engineering project cannot
honestly schedule a task whose difficulty is not yet known.

Durations for the graphics phase are the least certain figures in this
document. They are ranges of intent, not commitments.

```mermaid
gantt
    title Arkchemy — Skylanders: Spyro's Adventure, Wii U to Switch
    dateFormat YYYY-MM-DD
    axisFormat %d %b

    section Feasibility
    Prior art and format research      :done, f1, 2026-08-13, 2026-08-20
    Dump and toolchain setup           :done, f2, 2026-08-15, 2026-08-22

    section Recompiler
    RPX loading and disassembly        :done, r1, 2026-08-17, 2026-08-25
    Function recovery and codegen      :done, r2, 2026-08-20, 2026-09-01
    Cafe OS shims                      :done, r3, 2026-08-24, 2026-09-05
    Codegen correctness audits         :active, r4, 2026-09-01, 2026-10-15

    section Formats
    igArchive container                :done, b1, 2026-08-22, 2026-08-30
    LZMA block compression             :done, b2, 2026-08-28, 2026-09-02
    igz structure and fixups           :done, b3, 2026-08-30, 2026-09-06
    Models, textures, animation        :b4, 2026-10-01, 2026-12-15

    section Boot
    First NRO on hardware              :done, o1, 2026-08-26, 2026-08-29
    Job queue and atomics              :done, o2, 2026-08-30, 2026-09-04
    Memory pools and frame managers    :done, o3, 2026-09-04, 2026-09-06
    Thread stack corruption            :done, o4, 2026-09-06, 2026-09-07
    Archive loading                    :active, o5, 2026-09-07, 2026-09-20

    section Content
    Level loading                      :c1, after o5, 21d
    Object instantiation               :c2, after c1, 21d

    section Graphics
    Surface and texture formats        :g1, 2026-10-15, 45d
    Shader translation R600 to Maxwell :crit, g2, 2026-11-01, 120d
    Command buffers and present        :g3, after g1, 45d
    First rendered frame               :milestone, m1, 2027-03-01, 0d

    section Later
    Audio                              :a1, 2027-03-01, 60d
    Input and Portal of Power          :a2, 2027-04-01, 60d

    section Ongoing
    Host-side test coverage            :active, t1, 2026-09-08, 2026-12-31
    Public reporting                   :active, t2, 2026-08-20, 2027-06-30
```

## Critical path

**Shader translation is the critical path**, and it is the only task marked
critical above. Everything in the Graphics section gates the first rendered
frame, and nothing in Audio, Input or the Online work can begin meaningfully
before that.

Three tasks can run genuinely in parallel because they touch different
subsystems: codegen audits, format research, and host-side test coverage. That
is why they are scheduled as overlapping rather than sequential.

## Dependencies worth stating

- Level loading depends on archive loading, which currently depends on a config
  merge that does not happen
- Model and texture format work feeds the graphics phase and should therefore
  *precede* it, which is why it starts in October rather than after
- Audio and input are genuinely independent of graphics and could be pulled
  forward if graphics stalls — worth remembering if the shader work proves as
  hard as expected

## Milestones

| Milestone | Criterion | Status |
| --- | --- | --- |
| M1 Translation complete | 100% of `.text` translated | Met 2026-09-01 |
| M2 Runs on hardware | `.nro` boots and executes guest code | Met 2026-08-29 |
| M3 Engine initialises | static init completes, threads run | Met 2026-09-07 |
| M4 Render loop | GX2 pipeline driven each frame | Met 2026-09-08 |
| M5 Archive loads fully | all 35 blocks, level opens | In progress |
| M6 First rendered frame | anything visible on screen | Planned |
| M7 Playable level | movement, collision, camera | Not scheduled |

## Honesty note

The graphics dates are the weakest part of this chart. GX2 shaders are
compiled AMD R600 machine code and deko3d expects offline-compiled Nvidia
Maxwell binaries; there is no established route between them. 120 days is a
guess informed by the fact that the shader set is finite and ships on the disc,
which makes pattern-matching a plausible fallback to full translation. If that
assumption is wrong the estimate is wrong by a lot, and the chart should be
revised rather than defended.
