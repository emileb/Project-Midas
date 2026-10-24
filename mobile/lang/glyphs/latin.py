"""Accented Latin for both fonts (CP1252 bytes): the font's own letter with a mark drawn over or under it."""
from glyphkit import art, lat, flip, cols, hcat, norm, width, LEVEL

# Marks per font: rows of shade characters, drawn centred on the letter's ink
MARKS = {
    "fontZurichBold": {
        "acute": [" -#", "-#."],
        "grave": ["#-", ".#-"],
        "circ": [" :#: ", ":#.#:"],
        "tilde": [".=#-.#", "#-.-#=."],
        "diaer": [":*  :*", ".-  .-"],
        "ring": [".*=.", "*..*", ".*=."],
        "cedilla": ["  :#", " -#:"],
    },
    "fontBankGothic": {
        "acute": [" :#", ":#."],
        "grave": ["#:", ".#:"],
        "circ": [" :#: ", ":#.#:"],
        "tilde": [".#=.#", "#.=#."],
        "diaer": ["#  #"],
        "ring": [".#.", "#.#", ".#."],
        "cedilla": [" #:"],   # one row: the watch clips glyphs at the line height
    },
}

# CP1252 byte -> (base letter, mark)
MARKED = {}
for base, marks in (("A", "grave acute circ tilde diaer ring"), ("E", "grave acute circ diaer"),
                    ("I", "grave acute circ diaer"), ("O", "grave acute circ tilde diaer"),
                    ("U", "grave acute circ diaer"), ("N", "tilde"), ("C", "cedilla"), ("Y", "acute")):
    for m in marks.split():
        upper = {"A": {"grave": 0xC0, "acute": 0xC1, "circ": 0xC2, "tilde": 0xC3, "diaer": 0xC4, "ring": 0xC5},
                 "E": {"grave": 0xC8, "acute": 0xC9, "circ": 0xCA, "diaer": 0xCB},
                 "I": {"grave": 0xCC, "acute": 0xCD, "circ": 0xCE, "diaer": 0xCF},
                 "O": {"grave": 0xD2, "acute": 0xD3, "circ": 0xD4, "tilde": 0xD5, "diaer": 0xD6},
                 "U": {"grave": 0xD9, "acute": 0xDA, "circ": 0xDB, "diaer": 0xDC},
                 "N": {"tilde": 0xD1}, "C": {"cedilla": 0xC7}, "Y": {"acute": 0xDD}}[base][m]
        MARKED[upper] = (base, m)
        MARKED[upper + 0x20] = (base.lower(), m)
MARKED[0xFF] = ("y", "diaer")

# Bytes drawn without a glyph of their own: folded to these in ge_lang.c
FOLDED = {0xC6, 0xD0, 0xD7, 0xD8, 0xDE, 0xE6, 0xF0, 0xF7, 0xF8, 0xFE}


def mark_rows(fname, m):
    rows = MARKS[fname][m]
    w = max(len(r) for r in rows)
    return [[LEVEL[c] for c in r.ljust(w)] for r in rows]


def ink_rows(g):
    return [i for i, r in enumerate(g['rows']) if max(r, default=0) > 100]


def centre(g):
    """Ink centre column of the letter's body."""
    tot = sum(v for r in g['rows'] for v in r)
    return sum(x * v for r in g['rows'] for x, v in enumerate(r)) / max(tot, 1)


def place(g, mrows, top, cx):
    """Max-composite mark rows onto glyph g with the mark's top at absolute row top, centred at column cx."""
    g = norm(dict(g, rows=[list(r) for r in g['rows']]))
    mw = len(mrows[0])
    x0 = int(round(cx - (mw - 1) / 2.0))
    if x0 < 0:   # widen to the left
        g['rows'] = [[0] * -x0 + r for r in g['rows']]
        x0 = 0
    w = max(width(g), x0 + mw)
    g['rows'] = [r + [0] * (w - len(r)) for r in g['rows']]
    while top < g['bl']:
        g['rows'].insert(0, [0] * w)
        g['bl'] -= 1
    while top + len(mrows) > g['bl'] + len(g['rows']):
        g['rows'].append([0] * w)
    for i, mr in enumerate(mrows):
        r = g['rows'][top - g['bl'] + i]
        for j, v in enumerate(mr):
            r[x0 + j] = max(r[x0 + j], v)
    return g


def marked(fname, base, m):
    g = lat(fname, base)
    if base == 'i' and 'Zurich' in fname:   # dotless: drop the dot above the x-height
        g = dict(g, rows=[[0] * len(r) if g['bl'] + i < 4 else r for i, r in enumerate(g['rows'])])
    rows = ink_rows(g)
    top_ink, bot_ink = g['bl'] + rows[0], g['bl'] + rows[-1]
    mrows = mark_rows(fname, m)
    if m == "cedilla":
        return place(g, mrows, bot_ink + 1, centre(g))
    return place(g, mrows, top_ink - 1 - len(mrows), centre(g))


def build(fname):
    """byte -> glyph for the marked letters and the extra characters."""
    L = lambda c: lat(fname, c)
    G = {b: marked(fname, base, m) for b, (base, m) in MARKED.items()}

    # ¡ ¿: the font's ! and ? turned upside down
    for b, c in ((0xA1, '!'), (0xBF, '?')):
        g = L(c)
        top = min(lat(fname, x)['bl'] for x in 'Hx')
        bot = lat(fname, 'H')['bl'] + len(lat(fname, 'H')['rows'])
        rows = flip(g)['rows'][::-1]
        # mirrored about the cap middle, then lowered to hang below the line as in print
        g = dict(g, rows=rows, bl=top + bot - (g['bl'] + len(g['rows'])) + (2 if 'Zurich' in fname else 0))
        G[b] = g

    # œ Œ: o and e joined, sharing the touching stroke column
    for b, o, e in ((0x9C, 'o', 'e'), (0x8C, 'O', 'E')):
        go, ge = L(o), L(e)
        G[b] = hcat(cols(go, 0, width(go) - 1), cols(ge, 1 if 'Zurich' in fname else 0), kern=go['kern'])

    if 'Zurich' in fname:
        G[0xDF] = art(0, """
  .:.
 =*+*=
:#: .#=
-#.  #-
-#. -#.
-#.+*:
-#.:+#=
-#.  :#
-#.  .%
-#.:-#+
-#.=*+.
""", L('h')['kern'])
    else:   # small capitals: ß is SS
        s = L('s')
        G[0xDF] = hcat(s, art(s['bl'], " "), s, kern=s['kern'])
    return G
