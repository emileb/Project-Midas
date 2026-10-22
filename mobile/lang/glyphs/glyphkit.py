"""Glyph helpers: a glyph is dict(bl, rows (list of lists of 0..255), kern (kerning index))."""
import struct
import zlib

from fontdump import load

SHADE = " .:-=+*#%@"
LEVEL = {c: (0 if i == 0 else min(255, i * 26 + 12)) for i, c in enumerate(SHADE)}

_fonts = {}


def font(name):
    if name not in _fonts:
        kern, chars, _ = load(name)
        _fonts[name] = (kern, chars)
    return _fonts[name]


def sbl(v):
    return v - (1 << 32) if v >= 1 << 31 else v


def lat(fname, c):
    ch = font(fname)[1][ord(c) - 0x21]
    return dict(bl=sbl(ch['baseline']), rows=[list(r) for r in ch['px']], kern=ch['kern'])


def art(bl, text, kern=0):
    lines = text.strip("\n").split("\n")
    w = max(len(l.rstrip()) for l in lines)
    rows = [[LEVEL[c] for c in l.rstrip().ljust(w)] for l in lines]
    return dict(bl=bl, rows=rows, kern=kern)


def width(g):
    return max((len(r) for r in g['rows']), default=0)


def norm(g):
    w = width(g)
    g['rows'] = [r + [0] * (w - len(r)) for r in g['rows']]
    return g


def flip(g, kern=None):
    g = norm(dict(g, rows=[list(r) for r in g['rows']]))
    g['rows'] = [r[::-1] for r in g['rows']]
    if kern is not None:
        g['kern'] = kern
    return g


def cols(g, a, b=None):
    g = norm(dict(g, rows=[list(r) for r in g['rows']]))
    g['rows'] = [r[a:b] for r in g['rows']]
    return g


def setrows(g, top, text):
    """Replace rows from absolute row `top` with art lines (absolute rows = bl + index)."""
    g = norm(dict(g, rows=[list(r) for r in g['rows']]))
    w = width(g)
    for i, l in enumerate(text.strip("\n").split("\n")):
        r = top + i - g['bl']
        while r < 0:
            g['rows'].insert(0, [0] * w)
            g['bl'] -= 1
            r += 1
        while r >= len(g['rows']):
            g['rows'].append([0] * w)
        row = [LEVEL[c] for c in l.rstrip()]
        w = max(w, len(row))
        g['rows'][r] = row + [0] * (w - len(row))
    return norm(g)


def hcat(*gs, kern=None):
    top = min(g['bl'] for g in gs)
    bot = max(g['bl'] + len(g['rows']) for g in gs)
    rows = [[] for _ in range(bot - top)]
    for g in gs:
        g = norm(g)
        w = width(g)
        for y in range(top, bot):
            r = y - g['bl']
            rows[y - top] += g['rows'][r] if 0 <= r < len(g['rows']) else [0] * w
    return dict(bl=top, rows=rows, kern=gs[0]['kern'] if kern is None else kern)


def trim(g):
    """Drop fully blank rows at top and bottom (keeps faint rows)."""
    g = dict(g, rows=[list(r) for r in g['rows']])
    while g['rows'] and not any(g['rows'][0]):
        g['rows'].pop(0)
        g['bl'] += 1
    while g['rows'] and not any(g['rows'][-1]):
        g['rows'].pop()
    return g


def show(g):
    out = []
    for i, r in enumerate(g['rows']):
        out.append("%3d |%s|" % (g['bl'] + i, "".join(SHADE[min(9, v * 10 // 256)] for v in r)))
    return "\n".join(out)


# ---- preview rendering ------------------------------------------------------------------------

def render(fname, glyphs, text, x0=2, y0=4):
    """glyphs: dict char -> glyph (Latin falls back to the font). Mirrors textRenderGlyph pen moves."""
    kern, chars = font(fname)
    items = []
    x = x0
    prev = lat(fname, 'H')
    for c in text:
        if c == ' ':
            x += 5
            prev = lat(fname, 'H')
            continue
        g = glyphs.get(c) or (lat(fname, c) if 0x21 <= ord(c) < 0x7F else lat(fname, '?'))
        k = kern[prev['kern'] * 13 + g['kern']]
        k = k - (1 << 32) if k >= 1 << 31 else k
        x -= k - 1
        items.append((x, g))
        x += width(g)
        prev = g
    return items, x + 2


def png(path, lines, scale=5):
    """lines: list of (fname, glyphs, text). Writes a grey PNG, white text on dark blue like the watch."""
    rendered = []
    W, H = 0, 0
    for fname, glyphs, text in lines:
        items, w = render(fname, glyphs, text)
        h = 18 if 'Zurich' in fname else 13
        rendered.append((items, H, h))
        W = max(W, w)
        H += h
    img = [[0] * W for _ in range(H)]
    for items, top, h in rendered:
        for x, g in items:
            for i, r in enumerate(g['rows']):
                y = top + 2 + g['bl'] + i
                for j, v in enumerate(r):
                    if 0 <= y < H and 0 <= x + j < W:
                        img[y][x + j] = max(img[y][x + j], v)
    raw = b''
    for y in range(H * scale):
        row = img[y // scale]
        line = bytearray()
        for x in range(W * scale):
            v = row[x // scale]
            line += bytes((v * 235 // 255 + 10, v * 235 // 255 + 20, v * 200 // 255 + 55))
        raw += b'\0' + bytes(line)

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W * scale, H * scale, 8, 2, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(data)
