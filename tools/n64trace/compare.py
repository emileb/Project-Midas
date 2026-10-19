#!/usr/bin/env python3
"""Compare a port demo log with an N64 one, tick by tick.

  compare.py port.log n64.log

port.log: the port run with GE_DEMO=<n> GE_DEMODIAG=1 ("ge: demo tick ...").
n64.log:  getrace --demo <n> output ("n64: demo tick ...").

Prints the first tick where the RNG draw count or the full 64-bit seed
differ, with a few ticks of context. That tick is where to point
getrace --trace and the port's GE_RNGTRACE.
"""
import re
import sys

PORT = re.compile(r'ge: demo tick (\d+) draws=(\d+) .*seed=([0-9a-f]{16})')
N64 = re.compile(r'n64: demo tick (\d+) draws=(\d+) seed=([0-9a-f]{16})')


def ticks(path, pat):
    out = {}
    for line in open(path, errors='replace'):
        m = pat.search(line)
        if m:
            out[int(m.group(1))] = (int(m.group(2)), m.group(3))
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    port = ticks(sys.argv[1], PORT)
    n64 = ticks(sys.argv[2], N64)
    common = sorted(set(port) & set(n64))
    if not common:
        sys.exit('no common ticks (does the port log have seed=? run with GE_DEMODIAG=1)')
    def differs(t):
        # tick 1's count includes draws made before the demo restores its
        # recorded seed, which the two sides count from different points
        return port[t][1] != n64[t][1] or (t > 1 and port[t][0] != n64[t][0])

    first = next((t for t in common if differs(t)), None)
    if first is None:
        print('identical for ticks %d-%d' % (common[0], common[-1]))
        return
    print('first difference at tick %d' % first)
    for t in common:
        if first - 3 <= t <= first + 3:
            mark = ' <<' if differs(t) else ''
            print('  tick %4d  port draws=%-4d seed=%s   n64 draws=%-4d seed=%s%s'
                  % (t, port[t][0], port[t][1], n64[t][0], n64[t][1], mark))


if __name__ == '__main__':
    main()
