#!/usr/bin/env python3
"""Writes a translation template from the English text banks (assets/obseg/text/L*E.c).

    extract_text.py <out.txt> [existing translation to carry over]

One entry per text slot, keyed <bank>.<slot> as langGet sees it (slot = index in the bank):

    # English text
    LdamE.4=Spanish text

\\n is a new line. An empty value, or a missing line, falls back to English in game.
"@lower <key>" reuses another entry lower-cased (the banks keep lower-case copies).
Banks the game never loads are left out.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
TEXT = os.path.join(ROOT, "assets", "obseg", "text")

# language.c LnameX_lookuptable order, without the banks no stage uses
BANKS = [
    ("LameE", "Library (multi)"), ("LarchE", "Archives"), ("LarkE", "Facility"), ("LashE", "Stack (multi)"),
    ("LaztE", "Aztec"), ("LcatE", "Citadel (multi)"), ("LcaveE", "Caverns"), ("LarecE", "Control"),
    ("LcradE", "Cradle"), ("LcrypE", "Egyptian"), ("LdamE", "Dam"), ("LdepoE", "Depot"),
    ("LdestE", "Frigate"), ("LdishE", "Temple (multi)"), ("LimpE", "Basement (multi)"), ("LjunE", "Jungle"),
    ("LlenE", "Cuba"), ("LoatE", "Caves (multi)"), ("LpeteE", "Streets"), ("LrefE", "Complex (multi)"),
    ("LrunE", "Runway"), ("LsevbE", "Bunker 2"), ("LsevE", "Bunker 1"), ("LsevxE", "Surface 1"),
    ("LsevxbE", "Surface 2"), ("LsiloE", "Silo"), ("LstatE", "Statue"), ("LtraE", "Train"),
    ("LgunE", "Weapons"), ("LtitleE", "Stage and menu titles"), ("LmpmenuE", "Multiplayer menus"),
    ("LpropobjE", "Pickups"), ("LmpweaponsE", "Multiplayer weapon sets"), ("LoptionsE", "Solo menus"),
    ("LmiscE", "Cheats"),
]

TOKEN = re.compile(r'\s*(?://[^\n]*|/\*.*?\*/|"((?:[^"\\]|\\.)*)"|(NULL|0)\b|,|\}|\{)', re.S)


def unescape(s):
    return re.sub(r'\\(.)', lambda m: {"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(m.group(1), m.group(1)), s)


def read_bank(name):
    """The bank's slots in order: a string, or None for an empty (0) slot."""
    # the US build's defines, as the port compiles them (CMakeLists.txt)
    src = subprocess.run(["cc", "-E", "-P", "-DVERSION_US", "-DLANG_US", "-DREFRESH_NTSC", "-x", "c",
                          os.path.join(TEXT, name + ".c")], check=True, capture_output=True).stdout.decode("latin-1")
    pos = src.index("{", src.index(name + "[]")) + 1
    slots, cur = [], None
    while True:
        m = TOKEN.match(src, pos)
        if m is None:
            raise SystemExit("%s: cannot parse at %r" % (name, src[pos:pos + 40]))
        pos = m.end()
        tok = m.group(0).strip()
        if tok.startswith("//") or tok.startswith("/*"):
            continue
        if m.group(1) is not None:
            cur = (cur or "") + unescape(m.group(1))   # adjacent literals concatenate
        elif m.group(2) is not None:
            slots.append(None)
        elif tok in (",", "}"):
            if cur is not None:
                slots.append(cur)
                cur = None
            if tok == "}":
                return slots


def escape(s):
    return s.replace("\\", "\\\\").replace("\n", "\\n")


def load_existing(path):
    out = {}
    if path and os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            if not line.startswith("#") and "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                out[k] = v
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    old = load_existing(sys.argv[2] if len(sys.argv) > 2 else None)
    lines = ["# GoldenEye 007 text: <bank>.<slot>=<text>, \\n = new line, empty = English",
             "# Keep each line about as long as the English: menus and boxes are a fixed size.", ""]
    count = 0
    for bank, title in BANKS:
        slots = read_bank(bank)
        lines.append("## %s - %s" % (bank, title))
        for i, s in enumerate(slots):
            if s is None or s.strip() == "":
                continue
            key = "%s.%d" % (bank, i)
            lower = next((j for j in range(i) if slots[j] and slots[j] != s and slots[j].lower() == s), None)
            lines.append("# " + escape(s))
            if lower is not None and key not in old:
                lines.append("%s=@lower %s.%d" % (key, bank, lower))
            else:
                lines.append("%s=%s" % (key, old.get(key, "")))
            count += 1
        lines.append("")
    open(sys.argv[1], "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    print("%d entries -> %s" % (count, sys.argv[1]))


if __name__ == "__main__":
    main()
