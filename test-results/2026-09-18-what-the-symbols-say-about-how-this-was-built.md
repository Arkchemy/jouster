# What the symbols say about how this game was built

Not a runtime finding. An archaeological one, from reading the symbol table
rather than running anything.

conquertron emits every recompiled function under its original mangled symbol,
so `jouster/game/source/` is, incidentally, the most complete public record of
the Alchemy engine's internals that exists: **20,714 functions across 1,604
distinct classes.**

Some of those symbols carry more than a name. Green Hills encodes the
translation unit a file-static came from, and that string is the **build
machine's absolute path**, flattened but intact.

## The toolchain

    C:\2011_2\hk2011_2_0_r1\Obj\cafe_ghs_5312\release\hkBase\...
    C:\Devdepot\Release\2011_2\Custom\2011_2_CafeSDK200\Obj\cafe_ghs_5312\...
    C:\Work\Projects\alchemy\release7_8\tfb\temp\cafe\ghs\fin\core\...
    C:\Work\Projects\tfb\engine\branches\japanese\temp\cafe\ghs\fingold\...

Which pins, exactly:

  * **Alchemy engine 7.8** -- `alchemy\release7_8`. The engine version has been
    inferred from behaviour in this project until now; this is the version
    string itself.
  * **Havok 2011.2.0-r1**, with a custom build against **Cafe SDK 2.00**.
  * **Green Hills Software** as the compiler -- `ghs`, build **5312**. Not GCC
    and not CodeWarrior, which matters: the mangling is GNU v2/ARM, which is
    why modern `c++filt` refuses every style for it, and why
    `tools/name-addr.py` carries its own decoder.
  * **Cafe** throughout, the Wii U's internal codename.

## Two build configurations

`fin` for the engine libraries, `fingold` for the game code:

    9091  fingold + tfb        the Toys for Bob game layer
    2547  fin + core (ig)      Alchemy core
    1307  fin + gfx (ig)       Alchemy graphics
     658  fin + attrs (ig)
     647  fin + sg (ig)
     225  fin + sdk/fmod       FMOD
     133  fin + movie/bink     Bink
      ..  fin + sdk/zlib, vorbisfile, mdct, codebook ...

`fingold` reads as *final gold* -- the gold-master configuration. The binary
Arkchemy recompiles is not a debug or a review build; it is the shipped master,
and it says so 9,214 times.

## The game code came out of a branch called `japanese`

Every one of the 9,214 Toys for Bob symbols carries the same tree:

    C:\Work\Projects\tfb\engine\branches\japanese\temp\cafe\ghs\fingold\

Not a handful. All of them. The entire game layer of this Wii U binary was
compiled from `branches\japanese`.

**Stated carefully:** the fact is the path. *Why* the Wii U SKU was built from
that branch is inference and nothing here establishes it -- a later SKU built
from a branch that had moved on is one plausible reading, and there are others.
Worth recording as an open question rather than an answer.

## Two classes worth naming

  * `tfbActor::tfbCorpus` -- an actor's physical body.
    `setupCollisionParameters`, `synchronizePhysicsActor`. A nice piece of
    naming for a physics proxy.
  * `tfbRfidTag::presenceEvent(Presence)` and `tfbSpyroTag::getPresence()` --
    the Portal of Power. A figure on the portal is a *presence*, and the
    peripheral classes are `tfbHardware::tfbRedOctaneTagScriptObject` and
    `tfbRedOctanePeripheralScriptObject`. RedOctane, who built the Guitar Hero
    peripherals, built the portal too, and the engine names it after them.

## Why this is worth a file

None of it changes a line of code. But "which Alchemy version, which Havok,
which compiler" are questions this project has been answering by inference for
weeks, and the answers were sitting in the symbol table the whole time. Phase 3
(the Portal) now has real class names to search for rather than guesses.

And `fingold` is a good word.
