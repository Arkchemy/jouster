# How Alchemy's reflection/registration works, and where ours stops

Sources: the Skylanders Reverse Engineering Discord (exports supplied by
the owner, 2026-08-28) and NefariousTechSupport's
[AlchemyMetadataDumper](https://github.com/NefariousTechSupport/AlchemyMetadataDumper).
Community findings below are theirs, not this project's — chiefly
**NefariousTechSupport**, with contributions from **bonesinmysoup**,
**hydos**, **winnernombre** and **currentlyrusty**. This file records
what they establish and what our own binary adds.

## The architecture (community)

Every class carries two generated methods:

- **`arkRegisterInternal`** — calls `Core::igArkRegister`, passing the
  class name, metaobject pointer, the *parent class's*
  `arkRegisterInternal`, the type size, the vtable pointer, and a
  pointer to its own `arkRegisterInitialize`.
- **`arkRegisterInitialize`** — supplies four arrays: `instFuncs`
  (function pointers to instantiate metafields), `fieldStorage`, `names`
  (field names) and `offsets` (field offsets, ushorts).

`igRegistry` is Alchemy's configuration system, and its config files are
the XML ones — `alchemy.xml` among them, which `igArkCore` holds a field
for. `igArkCore` owns the runtime reflection tables; the dumper's header
gives its layout for PS3/Wii on SSA-through-Imaginators:

    _unk00[0x24]
    +0x24  igTUHashTable<igMetaObject*, const char*>* _metaObjectHashTable
    +0x28  igTUHashTable<igMetaEnum*,   const char*>* _metaEnumHashTable

and each platform resolves the engine through a single static pointer,
e.g. Wii SSA: `Core_ArkCore = 0x8066AA94`, `Core_MetaFieldList =
0x8066AAA4` — 0x10 apart, in a cluster of engine globals.

## What our Wii U binary adds

The boot chain, read out of the regenerated C:

    arkchemy_game_entry -> main (0x2002bf0)
      -> Core::igRefAlchemy (0x21486a0)
        -> Core::igArkCore::igArkCore (0x2146ffc)
          -> Core::igArkCore::beginArkRegister (0x2154d74)
            -> callClassRegistrationFunctions (0x2154c08)
              -> per-class arkRegisterInternal -> Core::igArkRegister

Measured on hardware (see test-results/):

- The registration functions are **never entered**.
- igRegistry's meta-object descriptor at 0x119f08 is **never read**,
  which rules out a dispatch-resolution fault in our recompiler: a
  failed indirect call would still have read the pointer first.
- Synthetic 435928 (guest 0x101345e8) is **read 9 times, always 0, never
  written**, by `Core::igDataList::setCapacity` (0x215db1c).

That address is not the ArkCore singleton, which was our first guess.
Its immediate neighbours in `.bss` are read by accessors like
`Core::igObjectList::getClassMetaBase`, so the cluster is the table of
**per-class cached `igMetaObject*` pointers** that registration
populates. Ours is simply one of them — the one `setCapacity` happens to
need. Every other class's cached pointer is in the same state, which is
why there is an `arkRegisterMetaValidate` per class across the binary
and none of them is being reached.

## Consequence

The engine never reads a single file of its own — not even
`alchemy.xml`. The only `FSOpenFile` in a full run is the movie the test
harness itself opens. So this is not "one type failed to register": the
reflection system never initialises, and everything downstream of it —
config, object directories, asset loading — is unreachable.

## Worth knowing for other titles

bonesinmysoup notes that a recompilation of one TfB game is largely
transferable to the others, the main difference being the `igArkCore`
material. The AlchemyMetadataDumper already carries a `TARGET_CAFE`
(Wii U) guard with no Wii U game wired up; if this port ever needs a
runtime metadata dump, adding SSA Wii U there is a smaller job than
writing one, and would benefit that project rather than duplicating it.
