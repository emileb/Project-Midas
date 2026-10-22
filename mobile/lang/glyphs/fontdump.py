"""Reads the Latin fonts (assets/font/*.c) as glyph rows, for building new glyphs that match them."""
import os
import re
import struct

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "assets", "font")


def load(name):
    """kerning[169], 94 glyphs (dict c, baseline, height, width, kern, px rows), unused."""
    t = open(os.path.join(SRC, name + ".c")).read()
    arrs = [[int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]+', m.group(1))]
            for m in re.finditer(r'\w+\[[^\]]*\]\s*=\s*\{(.*?)\};', t, re.S)]
    kern, table, data = arrs[0], arrs[1], arrs[2]
    # the segment image: kerning, fontchar table, glyph bytes (glyph offsets are from its start)
    img = b''.join(struct.pack('>I', w) for w in kern + table + data)
    chars = []
    for i in range(94):
        idx, bl, h, w, ki, off = table[i * 6:i * 6 + 6]
        stride = (w + 7) & ~7
        chars.append(dict(c=chr(0x21 + i), baseline=bl, height=h, width=w, kern=ki,
                          px=[img[off + r * stride: off + r * stride + w] for r in range(h)]))
    return kern, chars, None
