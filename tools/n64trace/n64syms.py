#!/usr/bin/env python3
"""N64 function names for GoldenEye (US) addresses, from the decomp tree.

  n64syms.py table [repo]       print 'ADDR NAME' for every function found
  n64syms.py annotate [repo]    copy stdin to stdout, appending 'name+0xoff'
                                to every 8-digit hex address (7Fxxxxxx/70xxxxxx)

Sources: asm files list every instruction's address; C functions carry
address comments ("Address 0x7F...", "NTSC address 0x...") or are named after
their address (sub_GAME_7F......). Coverage is partial (about 1200 functions),
so a name is the nearest known function at or below the address: treat
'name+0xBIG' as "somewhere after name" and confirm with a breakpoint.
"""
import bisect
import os
import re
import sys

ASM_LINE = re.compile(r'/\*\s*[0-9A-Fa-f]{6}\s+([0-9A-Fa-f]{8})\s')
GLABEL = re.compile(r'^\s*glabel\s+(\w+)')
CFUNC = re.compile(r'^[A-Za-z_][\w \*]*?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*(\{)?\s*$')
ADDR_CMT = re.compile(r'(?:0x|\b)(7[0F][0-9A-Fa-f]{6})\b')
NAMED = re.compile(r'_(7[0F][0-9A-Fa-f]{6})$')
SKIP = {'if', 'while', 'for', 'switch', 'return', 'sizeof'}


def build(root):
    syms = {}
    for dp, _, fn in os.walk(os.path.join(root, 'src')):
        for f in fn:
            p = os.path.join(dp, f)
            try:
                lines = open(p, encoding='latin-1').read().split('\n')
            except OSError:
                continue
            if f.endswith('.s'):
                pend = None
                for line in lines:
                    m = GLABEL.match(line)
                    if m:
                        pend = m.group(1)
                        continue
                    m = ASM_LINE.search(line)
                    if m and pend:
                        syms.setdefault(int(m.group(1), 16), pend)
                        pend = None
            elif f.endswith('.c'):
                for i, line in enumerate(lines):
                    m = CFUNC.match(line)
                    if not m or m.group(1) in SKIP:
                        continue
                    nxt = lines[i + 1].strip() if i + 1 < len(lines) else ''
                    if not (m.group(2) or nxt.startswith('{')):
                        continue
                    name = m.group(1)
                    a = NAMED.search(name)
                    if a:
                        syms.setdefault(int(a.group(1), 16), name)
                        continue
                    for j in range(i - 1, max(i - 14, -1), -1):
                        a = ADDR_CMT.search(lines[j])
                        if a:
                            syms.setdefault(int(a.group(1), 16), name)
                            break
    return sorted(syms.items())


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else 'table'
    root = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', '..')
    syms = build(root)
    if mode == 'table':
        for a, n in syms:
            print('%08X %s' % (a, n))
        return
    addrs = [a for a, _ in syms]

    def name(m):
        a = int(m.group(0), 16)
        i = bisect.bisect_right(addrs, a) - 1
        if i < 0:
            return m.group(0)
        return '%s (%s+0x%x)' % (m.group(0), syms[i][1], a - syms[i][0])

    hexaddr = re.compile(r'\b7[0Ff][0-9A-Fa-f]{6}\b')
    for line in sys.stdin:
        sys.stdout.write(hexaddr.sub(name, line))


if __name__ == '__main__':
    main()
