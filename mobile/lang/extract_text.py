#!/usr/bin/env python3
r"""Writes a translation template from the English text banks (assets/obseg/text/L*E.c).

    extract_text.py <out.txt> [existing translation to carry over]

One entry per text slot, keyed <bank>.<slot> as langGet sees it (slot = index in the bank):

    # English text
    LdamE.4=Spanish text

\\n is a new line. An empty value, or a missing line, falls back to English in game.
"@lower <key>" reuses another entry lower-cased (the banks keep lower-case copies).
Banks the game never loads are left out.

The port's own menus are C strings, not bank slots: they are keyed by the English itself,

    P:Rebind Inputs\n=Spanish text

(\= for an "=" in the English) and swapped where text is drawn.
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

# The port's menus (watch pages, Port Options, front-end sheets): drawn strings end in \n
PORT_FILES = ["port/src/port_watchpages.c", "port/src/port_options.c", "port/src/input_pc.c", "src/game/front.c",
              "src/game/options.c", "src/game/bondview2.c"]
PORT_SKIP = re.compile(r'(printf|portConLogf|LOG|fprintf|fputs|snprintf|SDL_Log|perror|portIniWrite|SDL_strcasecmp|strcmp|getenv|fopen)\s*\(')

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


def split_entry(line):
    """key, value at the first "=" that isn't escaped as \\=."""
    m = re.match(r'((?:[^=\\]|\\.)*)=(.*)$', line)
    return (m.group(1), m.group(2)) if m else (None, None)


def load_existing(path):
    out = {}
    if path and os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            if not line.startswith("#"):
                k, v = split_entry(line.rstrip("\n"))
                if k:
                    out[k] = v
    return out


def port_strings():
    """The port's menu strings, in file order, without logs, ini text and assembler."""
    lit = re.compile(r'"((?:[^"\\]|\\.)*)"')
    seen = []
    for rel in PORT_FILES:
        for line in open(os.path.join(ROOT, rel), encoding="latin-1"):
            if PORT_SKIP.search(line) or line.strip().startswith(("*", "//", "/*")):
                continue
            for s in lit.findall(line):
                if (s.endswith("\\n") and "%" not in s and re.search("[A-Za-z]{2}", s) and len(s) < 90
                        and not re.match(r'(\s|\[|\.|\(|ge[: ])', s) and '\\"' not in s
                        and s not in seen):
                    seen.append(s)
    return seen


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
    lines.append("## PORT - the port's own menus (keyed by the English)")
    for s in port_strings():
        key = "P:" + s.replace("=", "\\=")
        lines.append("# " + s)
        lines.append("%s=%s" % (key, old.get(key, "")))
        count += 1
    lines.append("")
    open(sys.argv[1], "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    print("%d entries -> %s" % (count, sys.argv[1]))


if __name__ == "__main__":
    main()
