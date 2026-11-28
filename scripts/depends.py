#!/usr/bin/env python3
"""Complete the depends= of the shipped modules of one target.

modpost writes depends= from the exports it knows. An out-of-tree build
knows neither the other out-of-tree sources' exports (ice uses pldmfw,
qed crc8) nor, on 5.10, the tree's own: a modules-only kernel build writes
modules-only.symvers, not Module.symvers. arc's loader picks the modules
that go with a selected one by modinfo depends, so a missing name leaves a
module that cannot load.

For every shipped module: the modules that export a symbol it uses (a
shipped one first, for a folder such as update/ then a top level one it
does not shadow, then DSM's own from the toolkit's Module.symvers; vmlinux
is no dependency) and that depends= does not name yet are added to its
.modinfo (objcopy --update-section). Nothing is removed.

usage: depends.py --out DIR [--subdir NAME] --symvers Module.symvers --nm NM --objcopy OBJCOPY
"""
import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kmod  # noqa: E402


def norm(n):
    return n.replace('-', '_')


def kos(d):
    if not os.path.isdir(d):
        return {}
    return {f[:-3]: os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith('.ko')}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--subdir')
    ap.add_argument('--symvers', required=True)
    ap.add_argument('--nm', required=True)
    ap.add_argument('--objcopy', required=True)
    o = ap.parse_args()

    vmlinux, dsm_owner = set(), {}
    for line in open(o.symvers):
        p = line.rstrip('\n').split('\t')
        if len(p) >= 3:
            if p[2] == 'vmlinux':
                vmlinux.add(p[1])
            else:
                dsm_owner.setdefault(p[1], os.path.basename(p[2]))

    mine = kos(os.path.join(o.out, o.subdir) if o.subdir else o.out)
    top = {}
    if o.subdir:
        shadow = {norm(m) for m in mine}
        top = {m: p for m, p in kos(o.out).items() if norm(m) not in shadow}

    syms = {m: kmod.symbols(p, o.nm) for m, p in list(mine.items()) + list(top.items())}
    owner = {}
    for group in (mine, top):
        for m in group:
            for x in sorted(syms[m][1]):
                owner.setdefault(x, m)

    done = []
    for m, path in mine.items():
        have = [d for d in kmod.first(kmod.modinfo(path), 'depends').split(',') if d]
        need = set()
        for x in syms[m][0]:
            if x in vmlinux:
                continue
            d = owner.get(x) or dsm_owner.get(x)
            if d and norm(d) != norm(m):
                need.add(d)
        add = sorted(d for d in need if norm(d) not in {norm(h) for h in have})
        if not add:
            continue
        items = [i for i in kmod.section(path, '.modinfo').split(b'\0')
                 if i and not i.startswith(b'depends=')]
        items.append(('depends=' + ','.join(have + add)).encode())
        tmp = path + '.modinfo'
        with open(tmp, 'wb') as f:
            f.write(b'\0'.join(items) + b'\0')
        subprocess.run([o.objcopy, '--update-section', '.modinfo=' + tmp, path], check=True)
        os.remove(tmp)
        got = [x for x in kmod.first(kmod.modinfo(path), 'depends').split(',') if x]
        if sorted(got) != sorted(have + add):
            sys.exit(f'{m}: depends not rewritten ({got})')
        done.append(f'{m}+{",".join(add)}')
    label = f'{o.subdir}/' if o.subdir else 'top level'
    print(f'## depends completed, {label} ({len(done)}): ' + (' '.join(done) or '-'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
