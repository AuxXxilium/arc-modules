#!/usr/bin/env python3
"""Check a configured kernel tree against the official kernel's .config.

configcheck.py <official.config> <baseline.config> <final.config> <requested.config> <abi-allow.list>

official  the dev toolkit's build/.config (what Synology built)
baseline  official run through this source's Kconfig (olddefconfig)
final     baseline plus our fragment, after olddefconfig
requested the fragment lines that were applied

Modules built in the final tree are loaded into the official kernel, so the
fragment may only add modules: a symbol going n -> m is what it is for. Any
other change from the baseline can alter vmlinux or a struct layout the
official kernel and its own modules share, and fails the build unless the
symbol matches a pattern in abi-allow.list (menu gates such as
CONFIG_NET_VENDOR_*, and bools that only change code inside a module).

Exit 1 when the final tree is not safe to build modules from.
"""
import fnmatch
import re
import sys

LINE = re.compile(r'^(CONFIG_[A-Za-z0-9_]+)=(.*)$')
UNSET = re.compile(r'^# (CONFIG_[A-Za-z0-9_]+) is not set$')


def load(path):
    cfg = {}
    for line in open(path, errors='replace'):
        line = line.rstrip('\n')
        m = LINE.match(line)
        if m:
            cfg[m.group(1)] = m.group(2)
            continue
        m = UNSET.match(line)
        if m:
            cfg[m.group(1)] = 'n'
    return cfg


def patterns(path):
    try:
        return [l.split('#')[0].strip() for l in open(path) if l.split('#')[0].strip()]
    except OSError:
        return []


def main():
    official, baseline, final, requested = (load(p) for p in sys.argv[1:5])
    allow = patterns(sys.argv[5])
    allowed = lambda s: any(fnmatch.fnmatchcase(s, p) for p in allow)
    get = lambda c, s: c.get(s, 'n')

    drift = sorted(s for s in set(official) | set(baseline) if get(official, s) != get(baseline, s))
    added, gates, errors, notes = [], [], [], []
    for s in sorted(set(baseline) | set(final)):
        a, b = get(baseline, s), get(final, s)
        if a == b:
            continue
        if a == 'n' and b == 'm':
            added.append(s)
        elif a == 'n' and b == 'y' and allowed(s):
            gates.append(s)
        elif a == 'n' and b not in ('y', 'm'):
            # an int/hex/string that was hidden behind a disabled parent
            notes.append(f'{s}: {a} -> {b}')
        elif allowed(s):
            notes.append(f'{s}: {a} -> {b}')
        else:
            errors.append(f'{s}: {a} -> {b}' + ('' if s in requested else '  (not requested: selected or defaulted)'))

    missed = []
    for s, v in sorted(requested.items()):
        got = get(final, s)
        if got != v:
            missed.append(f'{s}={v} came out {got}')

    print(f'modules added (n -> m): {len(added)}')
    print(f'allowed bools (n -> y): {len(gates)}: ' + ' '.join(gates))
    for n in notes:
        print(f'allowed change: {n}')
    if drift:
        print(f'toolkit/source drift (official -> baseline, not ours): ' +
              ' '.join(f'{s}={get(official, s)}->{get(baseline, s)}' for s in drift))
    if missed:
        print(f'requested but not taken by Kconfig ({len(missed)}):')
        for m in missed:
            print(f'  {m}')
    if errors:
        print(f'ABI changes NOT allowed ({len(errors)}):')
        for e in errors:
            print(f'  {e}')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
