#!/usr/bin/env python3
"""prune.py <linux> <make.log> <baseline.config> <current.config>

A driver the fragment turns on may not compile against this kernel source
(a Kconfig dependency the official .config cannot satisfy, an old driver).
Kbuild then never reaches modpost, so no module of the whole tree gets
linked. This maps every object make reported as failed to the Kconfig
symbol that builds it and prints, one per line,

    CONFIG_SYMBOL<TAB>object<TAB>first error

for the symbols this build added (n in the baseline), which build.sh turns
off before the next pass. A failed object of a module the official kernel
itself builds (a borrowed or newer source against the official headers) is
printed as "OFFICIAL<TAB>module object<TAB>error": its symbol cannot change,
so build.sh takes that module out of its Makefile instead, and DSM's own
copy of it is the one that loads.
"""
import os
import re
import sys

FAILED = re.compile(r'\*\*\* \[[^\]]*?:\d+: ([^\]\s]+\.o)\] Error')
ERROR = re.compile(r'^([^\s:]+\.[ch]):\d+:\d+: (?:fatal )?error: (.*)$')
OBJ_RULE = re.compile(r'^\s*obj-\$\((CONFIG_[A-Za-z0-9_]+)\)\s*[+:]?=\s*(.*)$')
PART_RULE = re.compile(r'^\s*([A-Za-z0-9_.-]+)-(?:\$\((CONFIG_[A-Za-z0-9_]+)\)|y|objs)\s*[+:]?=\s*(.*)$')


def load_cfg(path):
    cfg = {}
    for line in open(path, errors='replace'):
        m = re.match(r'^(CONFIG_[A-Za-z0-9_]+)=(.*)$', line.strip())
        if m:
            cfg[m.group(1)] = m.group(2)
    return cfg


def makefile_lines(d):
    for f in ('Kbuild', 'Makefile'):
        p = os.path.join(d, f)
        if os.path.isfile(p):
            return open(p, errors='replace').read().replace('\\\n', ' ').splitlines()
    return []


def symbol_for(linux, obj):
    """CONFIG symbol(s) that make `obj` (relative to linux/) get built."""
    d, name = os.path.split(obj)
    lines = makefile_lines(os.path.join(linux, d))
    found = []
    for line in lines:
        m = OBJ_RULE.match(line)
        if m and name in m.group(2).split():
            found.append(m.group(1))
    if found:
        return found
    # part of a composite module: foo-y += name / foo-$(CONFIG_BAR) += name
    for line in lines:
        m = PART_RULE.match(line)
        if m and name in m.group(3).split():
            if m.group(2):
                found.append(m.group(2))
            else:
                found += symbol_for(linux, os.path.join(d, m.group(1) + '.o'))
    if found:
        return found
    # the directory itself is pulled in by its parent (obj-$(CONFIG_X) += dir/)
    parent, base = os.path.split(d)
    if parent:
        for line in makefile_lines(os.path.join(linux, parent)):
            m = OBJ_RULE.match(line)
            if m and base + '/' in m.group(2).split():
                found.append(m.group(1))
    return found


def module_obj(linux, obj):
    """The obj-$(CONFIG_..) object that `obj` ends up in (itself, or the
    composite module it is a part of), relative to linux/."""
    d, name = os.path.split(obj)
    lines = makefile_lines(os.path.join(linux, d))
    for line in lines:
        m = OBJ_RULE.match(line)
        if m and name in m.group(2).split():
            return obj
    for line in lines:
        m = PART_RULE.match(line)
        if m and name in m.group(3).split():
            return module_obj(linux, os.path.join(d, m.group(1) + '.o'))
    return obj


def selectors(linux, sym):
    """Symbols whose Kconfig entry has "select <sym>" (or "imply")."""
    name = sym[len('CONFIG_'):]
    sel = re.compile(r'^\s*(?:select|imply)\s+' + re.escape(name) + r'\b')
    ent = re.compile(r'^\s*(?:menu)?config\s+([A-Za-z0-9_]+)\s*$')
    out = []
    for d, _, files in os.walk(linux):
        for f in files:
            if not f.startswith('Kconfig'):
                continue
            cur = None
            for line in open(os.path.join(d, f), errors='replace'):
                m = ent.match(line)
                if m:
                    cur = m.group(1)
                elif cur and sel.match(line):
                    out.append('CONFIG_' + cur)
    return out


def main():
    if sys.argv[1] == '--selectors':
        # prune.py --selectors <linux> <baseline> <current> SYM...: the
        # selectors we added that keep SYM on, as "SYM2<TAB>-<TAB>selects SYM"
        # (build.sh adds why SYM must stay off)
        linux, base, cur = sys.argv[2], load_cfg(sys.argv[3]), load_cfg(sys.argv[4])
        for sym in sys.argv[5:]:
            for s in selectors(linux, sym):
                if cur.get(s, 'n') != 'n' and base.get(s, 'n') == 'n':
                    print(f'{s}\t-\tselects {sym}')
        return 0
    linux, log, baseline, current = sys.argv[1:5]
    base, cur = load_cfg(baseline), load_cfg(current)
    text = open(log, errors='replace').read().splitlines()
    first_error = {}
    for line in text:
        m = ERROR.match(line)
        if m:
            src = os.path.splitext(m.group(1))[0] + '.o'
            first_error.setdefault(src, m.group(2))
    failed = []
    for line in text:
        m = FAILED.search(line)
        if m and m.group(1) not in failed:
            failed.append(m.group(1))
    rc = 0
    seen = set()
    for obj in failed:
        err = first_error.get(obj) or next(
            (e for s, e in first_error.items() if os.path.dirname(s) == os.path.dirname(obj)), 'link/other error')
        syms = [s for s in symbol_for(linux, obj) if cur.get(s, 'n') != 'n']
        ours = [s for s in syms if base.get(s, 'n') == 'n']
        if not ours:
            mod = module_obj(linux, obj)
            if mod not in seen:
                seen.add(mod)
                print(f'OFFICIAL\t{mod}\t{err}')
            continue
        for s in ours:
            if s not in seen:
                seen.add(s)
                print(f'{s}\t{obj}\t{err}')
    return rc


if __name__ == '__main__':
    sys.exit(main())
