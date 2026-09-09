# Software development lifecycle — Arkchemy

How this project is actually built, mapped onto the standard lifecycle. Written
both as a record of practice and as a reference for the SDLC as it applies to a
reverse-engineering project, where several textbook assumptions do not hold.

---

## 1. Choice of methodology

| Model | Fit here | Why |
| --- | --- | --- |
| **Waterfall** | Poor | Requires requirements to be knowable up front. Here the specification *is* the retail binary, and it is discovered by reading it. |
| **V-model** | Poor | Same problem, plus it pairs each design stage with a test stage — but the acceptance criterion ("does it behave like the original") cannot be written before the original is understood. |
| **Incremental / iterative** | **Adopted** | Each milestone is a runnable artefact. Boot further, load more, draw something. |
| **Spiral** | **Adopted in part** | Risk-driven: the highest-risk unknown is tackled first each cycle. Shader translation was deliberately deferred *because* it was correctly identified as the biggest risk, not despite it. |
| **Agile / Scrum** | Partial | No sprints or ceremonies — a one-developer project with an AI pair. But short cycles, working software over documentation, and responding to change are all present. |
| **RAD / prototyping** | Used tactically | `hosttest` is a throwaway-prototype technique: build the smallest thing that answers one question. |
| **DevOps** | Partial | Continuous deployment to the target device every cycle; automated publishing of the public site. No CI yet — see §9. |

**In practice: risk-driven incremental development with very short feedback
cycles.** The unit of work is one *measurement*, not one feature.

### The dominant constraint

Most projects iterate against a specification. This one iterates against an
**oracle** — the retail game running under Cemu — and against the binary itself.
That changes the lifecycle:

- Requirements are *recovered*, not elicited
- "Correct" means "behaves as the original does", which is testable but only
  by comparison
- A large share of effort is **diagnosis**, which the standard SDLC barely
  models

---

## 2. Feasibility study

Assessed along the usual axes:

- **Technical.** Static recompilation of a PowerPC title to ARM64 is proven
  practice (N64 and GameCube/Wii projects). The unknown was Alchemy's engine and
  the GX2 graphics layer. **Verdict: feasible with a large graphics risk.**
- **Economic.** Zero budget; hardware already owned. Cost is time.
- **Legal.** The user supplies their own dump. No game code or assets are
  distributed. Each repository carries `LEGAL.md`. **This is a hard constraint,
  not a preference** — see §10.
- **Operational.** Deployment is copying an `.nro` to an SD card. No users to
  train, no data to migrate.
- **Schedule.** Open-ended hobby project; no external deadline.

---

## 3. Requirements

### Functional

| ID | Requirement | Status |
| --- | --- | --- |
| F1 | Translate 100% of the retail `.text` to portable C | Met (1,450,489 instructions; 28 bytes unrecovered) |
| F2 | Run the translated code on Switch hardware | Met |
| F3 | Complete engine initialisation | Met |
| F4 | Load and decompress game archives | Partial — 1 of 35 blocks |
| F5 | Load a level | Not started |
| F6 | Render a frame | Pipeline runs; zero draw calls |
| F7 | Audio | Not started |
| F8 | Input, including the Portal of Power | Not started |

### Non-functional

- **Correctness over coverage.** A translated instruction that runs but behaves
  differently is worse than one that fails loudly.
- **No game content in any repository.** Verifiable by inspection.
- **Reproducibility.** Any result must be reproducible from a stated build hash
  and config.
- **Honesty of reporting.** Public progress must not overstate. Encoded as a
  standing rule: no category is ever shown above 95%.

### How requirements are captured

Not from stakeholders — from the binary. The `.rpx` retains 175,174 symbols, so
class layouts, vtable slots and function boundaries are *lookups*. Where the
binary is ambiguous, the oracle (Cemu) settles it. Where both are ambiguous,
domain experts in the Skylanders RE community are asked directly.

---

## 4. Design

### Architecture

```
retail .rpx  ──▶  conquertron  ──▶  generated C  ──▶  ARM64 build  ──▶  .nro
                       │                                   │
                  ppc_runtime.h                       cafeos_*.h shims
                 (CPU + memory model)               (Wii U OS emulation)
```

Five repositories, separated by concern:

| Repo | Responsibility |
| --- | --- |
| `conquertron` | The recompiler, guest runtime, and OS shims |
| `jouster` | The Switch application and its test harness |
| `blaster` | File-format research and tooling |
| `woodburrow` | Public site and progress reporting |
| `armory` | Online/locker design (future) |

### Key design decisions, and their trade-offs

- **Static recompilation, not emulation.** Far faster and debuggable as C; but
  self-modifying code and indirect branches must be handled at translation
  time, and a mistranslation is silent.
- **Flat guest memory array, masked accesses.** Simple and fast; but an
  out-of-bounds guest access cannot be detected — the exact class of bug that
  cost four sessions.
- **Real pthreads for guest threads.** Genuine concurrency; but guest register
  state must be fabricated correctly, and a missing 16-byte stack linkage area
  corrupted a heap.
- **Shims over reimplementation.** Fast to write; but each shim is hand-written
  and under-scrutinised relative to the generated code. Both recent root causes
  were in shims.

---

## 5. Implementation

- **Language:** C++17 (recompiler), C99 (generated code and runtime), Python
  (tooling), JS/HTML (site).
- **Toolchain:** CMake, Capstone, devkitA64/libnx.
- **Version control:** Git, five repositories, conventional `main` branch.
  Commit messages carry the *reasoning*, not just the change — including
  retractions, because a wrong conclusion that was published needs to be
  visibly withdrawn.
- **Coding standards:** match surrounding style; comments explain *why*,
  particularly where a value was measured rather than assumed.

---

## 6. Testing

### Levels

| Level | How it is done here |
| --- | --- |
| **Unit** | `conquertron/hosttest` compiles a recompiled translation unit natively and exercises it in isolation — milliseconds per run |
| **Integration** | Trace capture and replay: 804 real allocator calls recorded on device and replayed on host and target |
| **System** | Full boot on Switch hardware, 25,200 frames, instrumented |
| **Acceptance** | Comparison against retail under Cemu — the oracle |
| **Regression** | Committed traces and `test-results/` notes; each build's findings recorded with its md5 |

### Verification vs validation

- **Verification** ("are we building it right"): does the translated code match
  PowerPC semantics? Done by instruction audit and host-side differential runs.
- **Validation** ("are we building the right thing"): does the game behave as
  the original? Done against Cemu.

### White box and black box

- **White box** dominates: the source of truth is disassembly, and probes are
  inserted at specific addresses with knowledge of the control flow.
- **Black box** appears at the system level: launch, observe, compare.

### Test data

Real game data only. Synthetic input has repeatedly failed to reproduce bugs
that a captured real trace reproduced immediately — a fresh TLSF heap accepted
every allocation the live run refused.

### Documented limitation

There is **no automated test suite and no CI**. Every result is a manual
hardware run. This is the project's largest process weakness and is on the
roadmap (§9).

---

## 7. Deployment

- Build produces `Jouster.nro` (~177 MB).
- Copied to the Switch SD card over MTP (`gio copy`; `cp` fails with I/O
  errors, throughput ~24 MB/s).
- Runtime configuration through `watch.cfg`, read at startup — many
  investigations need no rebuild at all.
- Every deployed build is identified by embedded `__DATE__ __TIME__` and md5,
  and both are recorded with the findings from that run. **A result without a
  build hash is not a result.**
- The public site deploys automatically via Vercel.

---

## 8. Maintenance

Categories, with real examples:

- **Corrective** — fixing defects. The thread-stack linkage area.
- **Adaptive** — responding to environment change. devkitPro moved from
  `/opt/devkitpro` to a staging path; the build had to follow.
- **Perfective** — improving what works. Keying the GX2 census on the function
  instead of the call site: not a bug fix, but the difference between 110
  attributed calls and 134,606.
- **Preventive** — reducing future defects. `docs/probe-design.md` exists so
  the same six probe-design mistakes are not made a seventh time.

---

## 9. Risk management

| Risk | Impact | Likelihood | Mitigation |
| --- | --- | --- | --- |
| Shader translation (R600 → Maxwell) proves infeasible | Project-ending for visuals | Medium | Shader set is finite and shipped; pattern-matching is a fallback to full translation |
| Silent codegen bugs | High — wrong behaviour, no warning | **Observed repeatedly** | Instruction audits; host-side differential testing; regression traces |
| Shim incorrectness | High | **Observed twice** | Audit shims against real Cafe OS semantics; treat them as first-class code |
| No CI or automated tests | Slows every change | Certain | Build out `hosttest` coverage; automate the build |
| Single developer, bus factor 1 | High | Certain | Everything written down; public repositories; findings recorded with reasoning |
| Legal challenge | Project-ending | Low | No game content distributed; user supplies own dump; `LEGAL.md` in every repo |
| Hardware iteration cost (~10 min/cycle) | Slows diagnosis | Certain | Move anything self-contained to `hosttest`; use `watch.cfg` to avoid rebuilds |

---

## 10. Legal and ethical considerations

- No copyrighted game code or assets are distributed. The user supplies a dump
  of a copy they own.
- The recompiler operates on a binary the user provides; the repositories
  contain analysis and tooling only.
- Attribution: derivative works with a visual front end must show the Arkchemy
  intro or logo — an explicit licence clause across all five repositories.
- Community research is credited, not absorbed. Where a finding came from
  another person, that is recorded.

---

## 11. Documentation produced

| Type | Where |
| --- | --- |
| Technical / architecture | `README.md`, `docs/address-space-split.md` |
| Format specifications | `blaster/FORMATS.md`, `blaster/IGZ.md` |
| Process | this document, `docs/probe-design.md` |
| Findings / lab record | `test-results/*.md` — one per hardware run, with build hash |
| Roadmap | `ROADMAP.md` in each repository |
| Public-facing | woodburrow site, Notion project log |
| Legal | `LEGAL.md` in each repository |

---

## 12. Evaluation against objectives

**Met:** full instruction translation; execution on real hardware; engine boot;
archive decompression; a complete render pipeline reaching the graphics layer.

**Not met:** level loading, drawing, audio, input.

**What the process got right:** short cycles with a measurement each; an oracle
to check against; writing findings down immediately, including wrong ones.

**What it got wrong:** debugging a self-contained allocator through a ten-minute
hardware loop for a week when it could have run on the host in milliseconds; and
six probe-design failures that each cost a full cycle. Both are now addressed —
one by `hosttest`, one by `probe-design.md` — but the lesson is that
**tooling investment was deferred too long**, which is a classic and
well-documented SDLC failure mode.
