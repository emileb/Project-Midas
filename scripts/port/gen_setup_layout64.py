#!/usr/bin/env python3
"""Derive the N64 -> 64-bit-host layout maps for setup-file records.

A ROM setup file is the N64 memory image: every struct is 4-byte scalars and
4-byte pointer slots. That is exactly the i386 layout of the same structs, so
the N64 offset of every field is what clang reports for -target i386. On a
64-bit host the pointer slots are 8 bytes and the records grow, so
port/src/romfile_setup.c re-lays every record out into a native image
(romfileSetupNative). It needs, for each record struct, where each N64 word
lands in the native struct - and that is derived here from
`-Xclang -fdump-record-layouts` for both targets, never written by hand.

Per struct the output is one u16 per N64 word: the native byte offset of that
word, with bit 15 set when the word is (or overlays, in a union) a pointer
slot (copied into the low half of an 8-byte slot, zero-extended).

Propdef types are mapped by the struct the game reads them through; other
structs the game casts the same record to ("aliases") must agree on every word
they cover, which is checked here. The N64 word counts come from
port/generated/setup_kinds.h (the ROM data's own record lengths); a record
struct shorter than its N64 record must be pointer-free (identity layout).

    python3 scripts/port/gen_setup_layout64.py [cc]

Output: port/generated/setup_layout64.h (layout tables only, no ROM data).
The header's _Static_asserts re-check every struct size against the
compiler (64-bit sizes on 64-bit hosts, N64/i386 sizes on 32-bit hosts), so a
struct edit that invalidates the tables breaks the build until this is rerun.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CC = sys.argv[1] if len(sys.argv) > 1 else "clang"
OUT = os.path.join(ROOT, "port", "generated", "setup_layout64.h")
KINDS = os.path.join(ROOT, "port", "generated", "setup_kinds.h")

FLAGS = ["-DAVOID_UB", "-DBUGFIX_R0", "-DLANG_US", "-DLEFTOVERDEBUG", "-DLEFTOVERSPECTRUM",
         "-DNONMATCHING", "-DREFRESH_NTSC", "-DVERSION_US", "-D_LANGUAGE_C", "-std=gnu11",
         "-funsigned-char", "-ffreestanding", "-fms-extensions", "-fcommon", "-Wno-everything"]
for inc in (".", "include", "include/ultra64", "include/PR", "src", "src/game", "src/inflate",
            "port/include", "src/libultra"):
    FLAGS.append("-I" + os.path.join(ROOT, inc))

# type -> (map struct, [(alias struct, words or None = all it covers)])
OBJ = [("ObjectRecord", None)]
OBJV = [("MissionObjectiveRecord", None)]
PROPDEFS = {
    0: ("PropDefHeaderRecord", []),
    1: ("DoorRecord", OBJ),
    2: ("GlobalDoorScaleRecord", []),
    3: ("ObjectRecord", []),
    4: ("KeyRecord", OBJ),
    5: ("ObjectRecord", []),
    6: ("CCTVRecord", OBJ),
    7: ("AmmoCrateRecord", OBJ),
    8: ("WeaponObjRecord", OBJ),
    9: ("GuardRecord", []),
    10: ("MonitorObjRecord", OBJ),
    11: ("MultiMonitorObjRecord", OBJ),
    12: ("ObjectRecord", []),
    13: ("AutogunRecord", OBJ),
    14: ("LinkRecord", []),
    15: ("PropDefHeaderRecord", []),
    16: ("PropDefHeaderRecord", []),
    17: ("HatRecord", OBJ),
    18: ("GuardAttributeRecord", []),
    19: ("LinkRecord", []),
    20: ("MultiAmmoCrateRecord", OBJ),
    21: ("BodyArmourRecord", OBJ),
    22: ("TagObjectRecord", []),
    23: ("MissionObjectiveRecord", [("objective_entry", None)]),
    24: ("PropDefHeaderRecord", []),
    25: ("DestroyObjectRecord", OBJV),
    26: ("CompleteConditionRecord", OBJV),
    27: ("FailConditionRecord", OBJV),
    28: ("CollectObjectRecord", OBJV),
    29: ("DepositObjectRecord", OBJV),
    30: ("PhotographObjectRecord", [("criteria_picture", None), ("MissionObjectiveRecord", 3)]),
    31: ("NULLObjectRecord", []),
    # objective_status.c reads ENTER_ROOM's word 2 through MissionObjectiveRecord;
    # word 3 is criteria_roomentered's runtime next pointer
    32: ("criteria_roomentered", [("MissionObjectiveRecord", 3)]),
    33: ("criteria_deposit", OBJV),
    34: ("CoopyObjectRecord", []),
    35: ("setup_objective_text", [("watchMenuObjectiveText", None), ("objective_entry", 3)]),
    36: ("ObjectRecord", []),
    37: ("RenameObjectRecord", [("textoverride", None)]),
    38: ("LockDoorRecord", []),
    39: ("VehichleRecord", OBJ),
    40: ("AircraftRecord", OBJ),
    41: ("PropDefHeaderRecord", []),
    42: ("GlassRecord", OBJ),
    43: ("SafeRecord", OBJ),
    44: ("SafeObjectRecord", []),
    45: ("TankRecord", OBJ),
    46: ("CutsceneRecord", []),
    47: ("TintedGlassRecord", OBJ),
    48: ("PropDefHeaderRecord", []),
}
INTROS = {
    0: "struct SetupIntroSpawn", 1: "struct SetupIntroItem", 2: "struct SetupIntroAmmo",
    3: "struct SetupIntroSwirl", 4: "struct SetupIntroAnim", 5: "struct SetupIntroCuff",
    6: "struct SetupIntroCamera", 7: "struct SetupIntroWatch", 8: "struct SetupIntroCredits",
    9: "struct SetupIntroEmpty",
}
SECTIONS = ["stagesetup", "PadRecord", "BoundPadRecord", "waypoint", "waygroup", "PathRecord",
            "AIListRecord", "pname"]
WIDE_SCALARS = ("uintptr_t", "intptr_t", "size_t", "long", "unsigned long", "ssize_t", "ptrdiff_t")


# names that are struct tags only (no typedef)
TAGS = {"criteria_roomentered", "criteria_deposit", "criteria_picture", "setup_objective_text",
        "watchMenuObjectiveText", "objective_entry"}


def cname(s):
    return s.replace("struct ", "")


def ctype(n):
    """the C spelling of a record name"""
    n = cname(n)
    return "struct " + n if (n in TAGS or n.startswith("SetupIntro")) else n


def read_kinds():
    words = {}
    intro = {}
    txt = open(KINDS).read()
    pd = txt.split("kSetupPropdefKinds")[1].split("kSetupIntroKinds")[0]
    for m in re.finditer(r"/\*\s*(\d+) \S+\s*\*/ \{\s*(\d+),", pd):
        words[int(m.group(1))] = int(m.group(2))
    it = txt.split("kSetupIntroKinds")[1]
    for m in re.finditer(r"/\*\s*(\d+) \S+\s*\*/ \{\s*(\d+),", it):
        intro[int(m.group(1))] = int(m.group(2))
    return words, intro


def dump(target):
    structs = sorted({s for s, _ in PROPDEFS.values()} | {a for _, al in PROPDEFS.values() for a, _ in al}
                     | set(INTROS.values()) | set(SECTIONS) | {"MonitorRecord"})
    src = '#include <ultra64.h>\n#include "bondtypes.h"\n#include "bondview.h"\n'
    for i, s in enumerate(structs):
        src += "int s_%d = sizeof(%s);\n" % (i, ctype(s))
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as f:
        f.write(src)
        path = f.name
    try:
        out = subprocess.run([CC] + target + FLAGS + ["-fsyntax-only", "-Xclang", "-fdump-record-layouts", path],
                             capture_output=True, text=True)
    finally:
        os.unlink(path)
    if out.returncode != 0:
        sys.exit("clang failed:\n" + out.stderr[:4000])
    return parse(out.stdout)


def parse(text):
    """{record name: (size, [(off, typestr, depth)])} from a layout dump."""
    recs = {}
    cur = None
    expect = False
    for line in text.splitlines():
        if "Dumping AST Record Layout" in line:
            expect = True
            continue
        if expect:
            m = re.match(r"\s*0 \| (?:struct |union )?(.+?)\s*$", line)
            cur, lines, expect = m.group(1), [], False
            continue
        if cur is None:
            continue
        m = re.search(r"\[sizeof=(\d+)", line)
        if m:
            recs.setdefault(cur, (int(m.group(1)), lines))
            cur = None
            continue
        m = re.match(r"\s*(\d+)(?::\d+-\d+)? \|( *)(.*)$", line)
        if m:
            lines.append((int(m.group(1)), (len(m.group(2)) - 1) // 2, m.group(3).strip()))
    return recs


def entries(recs, name, base=0):
    """flatten a record into (offset, typestr) leaves, expanding arrays of records"""
    size, lines = recs[name]
    out = []
    for off, depth, t in lines:
        m = re.match(r"(?:struct |union )?(\w+)\[(\d+)\] \w+$", t)
        if m and m.group(1) in recs:
            esz = recs[m.group(1)][0]
            for k in range(int(m.group(2))):
                out += entries(recs, m.group(1), base + off + k * esz)
            continue
        out.append((base + off, t))
    return out


def is_ptr(t):
    t = re.sub(r"\((?:anonymous|unnamed) at [^)]*\)", "", t)
    if "*" in t:
        return True
    head = re.sub(r"\s+\w+$", "", t)
    return head in WIDE_SCALARS


def wordmap(r32, r64, name):
    s32, _ = r32[name]
    s64, _ = r64[name]
    e32 = entries(r32, name)
    e64 = entries(r64, name)
    if len(e32) != len(e64):
        sys.exit("%s: the two layout dumps differ in shape" % name)
    words = []
    for w in range((s32 + 3) // 4):
        b = w * 4
        best = None
        cands = []
        for (o32, t), (o64, _) in zip(e32, e64):
            if o32 <= b and (best is None or o32 > best):
                best = o32
        for (o32, t), (o64, _) in zip(e32, e64):
            if o32 == best:
                cands.append((o64, t))
        n64 = {o64 for o64, _ in cands}
        if len(n64) != 1:
            sys.exit("%s word %d: ambiguous native offset %s" % (name, w, n64))
        o64 = n64.pop() + (b - best)
        ptr = best == b and any(is_ptr(t) for _, t in cands)
        if ptr and o64 % 8:
            sys.exit("%s word %d: pointer slot at native offset %d is not 8-aligned" % (name, w, o64))
        words.append(o64 | (0x8000 if ptr else 0))
    return s32, s64, words


def main():
    r32 = dump(["-target", "i386-linux-gnu"])
    r64 = dump(["-target", "arm64-apple-macos11"])
    r64x = dump(["-target", "x86_64-linux-gnu"])
    pdwords, introwords = read_kinds()
    maps = {}
    problems = []

    def get(name):
        n = cname(name)
        if n not in maps:
            if n not in r32:
                sys.exit("no layout for %s" % n)
            maps[n] = wordmap(r32, r64, n)
            if wordmap(r32, r64x, n) != maps[n]:
                problems.append("%s: x86_64 and arm64 layouts differ" % n)
        return maps[n]

    rows = []
    for t in range(49):
        name, aliases = PROPDEFS[t]
        s32, s64, wm = get(name)
        n64 = pdwords[t]
        if n64 * 4 > s32 and s32 != s64:
            problems.append("type %d: %s is %d bytes but the N64 record is %d words and the struct has pointers"
                            % (t, name, s32, n64))
        for a, lim in aliases:
            as32, as64, awm = get(a)
            cover = min(len(awm), n64, lim if lim is not None else 1 << 30)
            for w in range(cover):
                mine = wm[w] if w < len(wm) else w * 4
                if (awm[w] & 0x7FFF) != (mine & 0x7FFF):
                    problems.append("type %d word %d: %s puts it at %d, alias %s at %d"
                                    % (t, w, name, mine & 0x7FFF, a, awm[w] & 0x7FFF))
        rows.append((t, name, n64, s32, s64))
    for t in range(10):
        get(INTROS[t])
        if introwords[t] * 4 != maps[cname(INTROS[t])][0]:
            problems.append("intro type %d: %s is %d bytes, N64 record %d words"
                            % (t, INTROS[t], maps[cname(INTROS[t])][0], introwords[t]))
    for s in SECTIONS:
        get(s)
    if problems:
        sys.exit("layout problems:\n  " + "\n  ".join(problems))

    o = []
    o.append("/* GENERATED by scripts/port/gen_setup_layout64.py - do not edit.")
    o.append(" * N64 (= i386) -> 64-bit host layout of the setup-file records, from clang's")
    o.append(" * record layout dumps of bondtypes.h for i386 and arm64 (x86_64 checked equal).")
    o.append(" * Per struct: the native byte offset of each N64 word; SL64_PTR marks a pointer")
    o.append(" * slot (the 4-byte N64 value goes in the low half, zero-extended).")
    o.append(" * Layout tables only - no ROM data. */")
    o.append("#ifndef SETUP_LAYOUT64_H")
    o.append("#define SETUP_LAYOUT64_H")
    o.append("#define SL64_PTR 0x8000u")
    o.append("#define SL64_OFS(m) ((m) & 0x7FFFu)")
    o.append("struct sl64map { u16 size32; u16 size64; u16 nwords; const u16 *word; };")
    for n in sorted(maps):
        s32, s64, wm = maps[n]
        o.append("static const u16 kSL64_%s[%d] = { %s };" % (n, len(wm), ", ".join(
            ("0x%04X" % v) if v & 0x8000 else str(v) for v in wm)))
    for n in sorted(maps):
        s32, s64, wm = maps[n]
        o.append("#define SL64_MAP_%s { %d, %d, %d, kSL64_%s }" % (n, s32, s64, len(wm), n))
    o.append("/* propdef type -> map struct (N64 record words from setup_kinds.h) */")
    o.append("#define SL64_PROPDEF_MAPS { \\")
    for t, name, n64, s32, s64 in rows:
        o.append("    /* %2d n64 %3d words */ SL64_MAP_%s, \\" % (t, n64, name))
    o.append("}")
    o.append("#define SL64_INTRO_MAPS { \\")
    for t in range(10):
        o.append("    /* %d */ SL64_MAP_%s, \\" % (t, cname(INTROS[t])))
    o.append("}")
    o.append("/* the struct each propdef type is laid out as (sizepropdef's 64-bit arm) */")
    o.append("#define SL64_PROPDEF_STRUCTS(X) \\")
    for t, name, n64, s32, s64 in rows:
        o.append("    X(%d, %s, %d) \\" % (t, ctype(name), n64))
    o.append("")
    o.append("/* the tables above are only valid while the structs keep these sizes */")
    o.append("#if defined(SL64_WANT_CHECKS) && !defined(TARGET_N64)")
    o.append("#if UINTPTR_MAX > 0xFFFFFFFFu")
    o.append("#define SL64_SIZE_CHECK(T, s32, s64) _Static_assert(sizeof(T) == (s64), \"rerun scripts/port/gen_setup_layout64.py: \" #T);")
    o.append("#else")
    o.append("#define SL64_SIZE_CHECK(T, s32, s64) _Static_assert(sizeof(T) == (s32), \"rerun scripts/port/gen_setup_layout64.py: \" #T);")
    o.append("#endif")
    for n in sorted(maps):
        s32, s64, wm = maps[n]
        o.append("SL64_SIZE_CHECK(%s, %d, %d)" % (ctype(n), s32, s64))
    o.append("#endif")
    o.append("#endif")
    open(OUT, "w").write("\n".join(o) + "\n")
    print("wrote %s (%d structs)" % (OUT, len(maps)))
    for t, name, n64, s32, s64 in rows:
        if n64 * 4 != s32:
            print("  note: type %2d %-24s N64 record %3d words, struct %3d bytes (i386) / %3d (64-bit)"
                  % (t, name, n64, s32, s64))


main()
