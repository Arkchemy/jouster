# The guest profile names the lever, and it has been in the Makefile since day one

Build `Sep 17 2026 23:28:48`.

    GUESTHOT samples=14341 distinct=64 overflow=4856

    11%  0x2588650  __sti___22_hkTypeInfoRegistry_cpp
     9%  0x214cf74  Core::igMetaField::reset
     6%  0x214d1dc  Core::igMetaField::resetByValue
     6%  0x214d258  Core::igMetaField::construct
     5%  0x214d260  Core::igMetaField::commission
     2%  0x2184120  Core::igRefMetaField::commission
     2%  0x21a564c  Core::igScopeLock::~igScopeLock
     1%  0x21aa110  Core::igScopeLock::igScopeLock
     1%  0x215e458  Core::igObject::isOfType
     1%  0x217c7f0  Core::igBidirectionalHeapMemoryPool::contains
     1%  0x2156510  Core::igCafeMutex::lock
     1%  0x2173648  Core::igMemoryPool::updateStatistics

The names come from the generated sources themselves, which carry the original
mangled symbols -- `0x214cf74` sits inside
`ppc_reset__Q2_4Core11igMetaFieldFPQ2_4Core8igObject`. No symbol map needed.

## What it says

**The metafield system is 28% of the run on its own** -- reset, resetByValue,
construct, commission, plus `igRefMetaField::commission`. Everything below it
is scope locks, a type check, and allocator bookkeeping.

Every single entry is a small, frequently-called leaf function.

## And the build is `-O0`

From the top of `jouster/game/Makefile`, written before the first build:

> -O0 deliberately for this first real build: fast, low-memory compiles across
> 213+ files are the real priority right now (does it link and run at all), not
> runtime speed -- **a real, separate optimization pass once this actually
> boots.**

It boots. It has booted for weeks.

`-O0` is worst exactly where this profile is concentrated: nothing inlines, and
every local round-trips through the stack. A four-line accessor called a million
times is the pathological case, and `igMetaField::reset` is a four-line
accessor called a million times.

The guest is 64% of a 0.28fps frame, so this is the largest lever available,
and it has been sitting in the Makefile the whole time with a note saying when
to pull it.

## Caveats, stated rather than discovered later

  * `overflow=4856` is 34% of samples, arriving after the 64-slot table filled.
    The profile is genuinely spread; the top twelve are 46% and the named
    cluster is real, but this is not the whole picture.
  * `g_ppc_current_pc` is set on function entry, so a sample names the last
    function entered, not an instruction. A long loop inside a function with no
    calls reads as that function. `__sti___22_hkTypeInfoRegistry_cpp` at 11% is
    the one to treat carefully for that reason -- it is a static initialiser
    and static init reports as finished, so 11% may be attribution rather than
    residence.
  * -O2 changes inlining, which changes which functions exist to be sampled.
    The next profile is not directly comparable to this one. The comparable
    numbers are `RUNRATE elapsed`, `GPUCNT frames` and `INPUT vpadreads`.

## The change

`ARK_OPT ?= -O2`, overridable with `make ARK_OPT=-O0`. Changing CFLAGS does not
invalidate existing objects, so this needs a clean rebuild to mean anything --
a first attempt relinked in 20 seconds and would have deployed an -O0 binary
wearing an -O2 Makefile.

What to compare, against `elapsed=365874ms` and `frames=103` for 14,400 host
frames:

  * `RUNRATE elapsed` -- the run getting shorter is the whole point
  * `GPUCNT frames` and `INPUT vpadreads` -- the game's own frame count, which
    should rise together
  * `draws` and `modules` in the tally -- these must stay at ~300 and 7. An
    optimised build that boots less far is a miscompilation, not a win, and
    that is the real risk of -O2 on 217 machine-translated translation units.
