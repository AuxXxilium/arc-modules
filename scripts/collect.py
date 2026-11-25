#!/usr/bin/env python3
"""Pick, check and copy the modules of one target.

What ships:
  * every module built that the official kernel does not have (its name is
    not in the dev toolkit's modules.order): what the fragment and the
    out-of-tree sources add,
  * official modules named in config/<series>/official.list (arc loads them
    before DSM's own module tree is there),
  * whatever a shipped module depends on, if it was built and is not one of
    DSM's own modules (those DSM loads itself),
minus config/<series>/exclude.list. config/<series>/<platform>.list can add
(+name) or drop (-name) names for one platform.

An out-of-tree module replaces the in-tree module of the same name.

Every shipped module must have the official vermagic and resolve all its
undefined symbols from vmlinux, from DSM's own modules (both in the toolkit's
Module.symvers) or from other shipped modules; one that does not is dropped (with what it needs) and
listed. Shipped files are stripped of debug info.
"""
import argparse
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kmod  # noqa: E402


def names(path):
    out = set()
    try:
        for line in open(path):
            line = line.split('#')[0].strip()
            if line:
                out.add(line)
    except OSError:
        pass
    return out


def find_kos(top):
    found = {}
    for d, _, files in os.walk(top):
        for f in files:
            if f.endswith('.ko'):
                found.setdefault(f[:-3], os.path.join(d, f))
    return found


def norm(n):
    return n.replace('-', '_')


def main():
    ap = argparse.ArgumentParser()
    for a in ('linux', 'oot', 'toolkit', 'official-list', 'exclude-list', 'platform-list',
              'nm', 'strip', 'release', 'out'):
        ap.add_argument('--' + a, required=True)
    o = ap.parse_args()

    intree = find_kos(o.linux)
    oot, twice = {}, []
    for name in sorted(os.listdir(o.oot)) if os.path.isdir(o.oot) else []:
        for m, p in find_kos(os.path.join(o.oot, name)).items():
            if m in oot:
                # e.g. Intel's auxiliary bus, which i40e, iavf and ice each
                # carry: the last source (by name) wins
                twice.append(f'{m} ({oot[m][1]}, {name})')
            oot[m] = (p, name)
    built = {m: (p, 'in-tree') for m, p in intree.items()}
    replaced = sorted(m for m in oot if m in built)
    built.update({m: (p, 'oot:' + src) for m, (p, src) in oot.items()})
    by_norm = {norm(m): m for m in built}

    official = set()
    for line in open(os.path.join(o.toolkit, 'modules.order')):
        official.add(os.path.basename(line.strip())[:-3])
    # What the running DSM already provides: vmlinux, and the official
    # modules of its own module tree (the toolkit's Module.symvers has both).
    vmlinux, dsm_mods = set(), set()
    for line in open(os.path.join(o.toolkit, 'build', 'Module.symvers')):
        parts = line.rstrip('\n').split('\t')
        if len(parts) >= 3:
            if parts[2] == 'vmlinux':
                vmlinux.add(parts[1])
            else:
                dsm_mods.add(parts[1])

    keep_official = names(o.official_list)
    exclude = names(o.exclude_list)
    plat = names(o.platform_list)
    keep_official |= {n[1:] for n in plat if n.startswith('+')}
    exclude |= {n[1:] for n in plat if n.startswith('-')}

    ship = set()
    for m, (p, origin) in built.items():
        if m in exclude:
            continue
        if origin.startswith('oot:') or m not in official or m in keep_official:
            ship.add(m)

    info = {m: kmod.modinfo(built[m][0]) for m in built}
    syms = {}

    def symbols(m):
        if m not in syms:
            syms[m] = kmod.symbols(built[m][0], o.nm)
        return syms[m]

    dropped = {}
    # dependency closure: pull in what modpost recorded as needed. An
    # official module DSM loads itself stays DSM's (it is not shipped unless
    # official.list names it); its exports count as available below.
    changed = True
    while changed:
        changed = False
        for m in sorted(ship):
            for dep in filter(None, kmod.first(info[m], 'depends').split(',')):
                d = by_norm.get(norm(dep))
                if d and d not in ship and d not in exclude and d not in official:
                    ship.add(d)
                    changed = True

    # vermagic
    for m in sorted(ship):
        vm = kmod.first(info[m], 'vermagic')
        if not vm.startswith(o.release + ' '):
            dropped[m] = f'vermagic "{vm}"'
    ship -= set(dropped)

    # a module exporting what vmlinux already exports would not load
    for m in sorted(ship):
        dup = sorted(symbols(m)[1] & vmlinux)
        if dup:
            dropped[m] = 'exports symbols vmlinux has: ' + ' '.join(dup[:8])
    ship -= set(dropped)

    # symbol resolution, to a fixed point
    while True:
        provided = vmlinux | dsm_mods
        for m in ship:
            provided |= symbols(m)[1]
        bad = {}
        for m in sorted(ship):
            missing = sorted(symbols(m)[0] - provided)
            if missing:
                bad[m] = 'unresolved: ' + ' '.join(missing[:8]) + (' ...' if len(missing) > 8 else '')
        if not bad:
            break
        dropped.update(bad)
        ship -= set(bad)

    os.makedirs(o.out, exist_ok=True)
    for m in sorted(ship):
        dst = os.path.join(o.out, m + '.ko')
        shutil.copyfile(built[m][0], dst)
        subprocess.run([o.strip, '--strip-debug', dst], check=True)

    print('## shipped')
    for m in sorted(ship):
        i = info[m]
        print(f'{m}  [{built[m][1]}{", official" if m in official else ""}]'
              f'  version={kmod.first(i, "version", "-")}  depends={kmod.first(i, "depends", "-") or "-"}')
    print()
    print(f'## dropped ({len(dropped)})')
    for m in sorted(dropped):
        print(f'{m}  [{built[m][1]}]  {dropped[m]}')
    print()
    print('## out-of-tree replaces in-tree: ' + (' '.join(replaced) or '-'))
    print('## built by more than one out-of-tree source: ' + (' '.join(twice) or '-'))
    missing_official = sorted(n for n in keep_official if n not in built)
    print('## official.list entries not built here: ' + (' '.join(missing_official) or '-'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
