#!/usr/bin/env python3
"""summary.py <reports-dir> [targets...]: one line per target report.

target | shipped | prebuilt | both | vermagic differs | dropped (symbol or
vermagic check) | turned off (does not compile / needs what the official
kernel lacks) | out-of-tree failed | headers replaced
"""
import os
import re
import sys


def section(text, head):
    m = re.search(r'^## ' + re.escape(head) + r'.*?\n(.*?)(?=^## |\Z)', text, re.S | re.M)
    return m.group(1) if m else ''


def main():
    d = sys.argv[1]
    names = sys.argv[2:] or sorted(f[:-4] for f in os.listdir(d) if f.endswith('.txt'))
    print('| target | shipped | prebuilt | both | vermagic differs | dropped | turned off | oot failed | headers replaced |')
    print('|---|---|---|---|---|---|---|---|---|')
    for t in names:
        p = os.path.join(d, t + '.txt')
        if not os.path.exists(p):
            print(f'| {t} | - | - | - | - | - | - | - | no report |')
            continue
        s = open(p, errors='replace').read()
        shipped = re.search(r'^modules: (\d+)', s, re.M)
        ref = re.search(r'^reference (\d+), built (\d+), common (\d+)', s, re.M)
        vm = re.search(r'^vermagic differs \((\d+)\)', s, re.M)
        dropped = re.search(r'^## dropped \((\d+)\)', s, re.M)
        off = [l.split('\t')[0] for l in section(s, 'turned off').splitlines() if '\t' in l]
        oot = [l.split(':')[0] for l in section(s, 'out-of-tree').splitlines() if 'FAILED' in l]
        hdr = re.search(r'^## headers replaced[^:]*: (\d+)', s, re.M)
        print(f'| {t} | {shipped.group(1) if shipped else "-"} | {ref.group(1) if ref else "-"} | '
              f'{ref.group(3) if ref else "-"} | {vm.group(1) if vm else "-"} | '
              f'{dropped.group(1) if dropped else "-"} | {len(off)} | {" ".join(oot) or "-"} | '
              f'{hdr.group(1) if hdr else "-"} |')
    return 0


if __name__ == '__main__':
    sys.exit(main())
