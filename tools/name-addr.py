#!/usr/bin/env python3
"""Name a guest address, or a whole probe line of them.

    ./tools/name-addr.py 0x214cf74 0x2588650
    grep '^GUESTHOT' /d/switch/Jouster/game-results.log | tail -1 | ./tools/name-addr.py

The project has no symbol map and does not need one: conquertron emits each
recompiled function with its original mangled symbol, and every function entry
sets g_ppc_current_pc to its own address. So an address appearing as

    g_ppc_current_pc = 0x214cf74u;

is inside the function whose definition precedes it in that file.

Done by hand on 2026-09-17 to read the first GUESTHOT profile, which turned
twelve bare addresses into igMetaField::reset and friends and pointed straight
at the build still being -O0. Every probe here reports addresses, so it is
worth a script.

The mangling is GNU v2 / ARM, which modern binutils dropped -- devkitA64's
c++filt rejects every style for it -- so the decoder below is local and
deliberately partial. It resolves the qualified name, which is the part that
answers "what is hot", and gives up gracefully on argument lists rather than
guessing at them.
"""
import pathlib
import re
import sys

SRC = pathlib.Path(__file__).resolve().parent.parent / "game" / "source"

SPECIAL = {"__ct": "<constructor>", "__dt": "<destructor>",
           "__as": "operator=", "__eq": "operator==", "__ne": "operator!=",
           "__nw": "operator new", "__dl": "operator delete"}


def read_len_name(s, i):
    """A length-prefixed component: 4Core -> ("Core", i_after)."""
    j = i
    while j < len(s) and s[j].isdigit():
        j += 1
    if j == i:
        return None, i
    n = int(s[i:j])
    return s[j:j + n], j + n


def qualified(s, i):
    """Q<count>_ followed by that many length-prefixed components."""
    m = re.match(r"Q(\d+)_", s[i:])
    if not m:
        name, j = read_len_name(s, i)
        return ([name] if name else []), j
    count, i = int(m.group(1)), i + m.end()
    parts = []
    for _ in range(count):
        name, i = read_len_name(s, i)
        if name is None:
            break
        parts.append(name)
    return parts, i


def demangle(sym):
    """Core::igMetaField::reset, or the raw symbol if it does not parse."""
    # CodeWarrior prefixes some symbols with __CPR<n>__ (a compression marker
    # for repeated substrings). It carries no meaning for a name and hides the
    # __ct/__dt that follows it.
    sym = re.sub(r"^__CPRd+__", "", sym)
    # Static initialisers and other __sti__ names are already readable.
    if sym.startswith("__sti__") or "__" not in sym:
        return sym
    head, _, tail = sym.partition("__")
    # A leading __ct/__dt means the name itself started with the separator.
    if head == "":
        for key, label in SPECIAL.items():
            if sym.startswith(key):
                head, tail = label, sym[len(key):].lstrip("_")
                break
        else:
            return sym
    parts, i = qualified(tail, 0)
    if not parts:
        return sym
    cls = "::".join(parts)
    name = head
    if name == "<constructor>":
        name = parts[-1]
    elif name == "<destructor>":
        name = "~" + parts[-1]
    const = "" 
    rest = tail[i:]
    if rest.startswith("C"):
        const, rest = " const", rest[1:]
    return f"{cls}::{name}(...){const}" if rest.startswith("F") else f"{cls}::{name}{const}"


def main():
    text = " ".join(sys.argv[1:])
    if not text and not sys.stdin.isatty():
        text = sys.stdin.read()
    addrs = [a.lower().removeprefix("0x")
             for a in re.findall(r"0x[0-9a-fA-F]{5,8}", text)]
    if not addrs:
        print("name-addr: no addresses in the input", file=sys.stderr)
        return 1
    if not SRC.is_dir():
        print(f"name-addr: no generated sources at {SRC}", file=sys.stderr)
        return 1

    # One pass over the sources for all addresses, rather than one grep each.
    want = {a: None for a in addrs}
    entry = re.compile(r"g_ppc_current_pc = 0x([0-9a-f]+)u;")
    fndef = re.compile(r"^void (ppc_[A-Za-z0-9_]*)\(PpcContext")
    for path in sorted(SRC.glob("*.c")):
        current = None
        with path.open(encoding="utf-8", errors="replace") as fh:
            for lineno, line in enumerate(fh, 1):
                m = fndef.match(line)
                if m:
                    current = m.group(1)[4:]
                    continue
                m = entry.search(line)
                if m and m.group(1) in want and want[m.group(1)] is None:
                    want[m.group(1)] = (current, path.name, lineno)
        if all(v is not None for v in want.values()):
            break

    for a in addrs:
        hit = want.get(a)
        if not hit or not hit[0]:
            print(f"0x{a:<8}  <no function entry at this address>")
            continue
        sym, fname, lineno = hit
        print(f"0x{a:<8}  {demangle(sym)}")
        print(f"{'':<12}{sym}")
        print(f"{'':<12}{fname}:{lineno}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
