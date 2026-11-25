#!/usr/bin/env python3
"""compare.py <reference-dir> <built-dir>: what a source build ships next to a
reference module folder (for example modules/<target>/ of the dev branch).

Prints the modules only one side has, and for the common ones a vermagic or
version difference."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kmod  # noqa: E402


def kos(d):
    return {f[:-3]: os.path.join(d, f) for f in os.listdir(d) if f.endswith('.ko')}


def main():
    ref, new = kos(sys.argv[1]), kos(sys.argv[2])
    only_ref = sorted(set(ref) - set(new))
    only_new = sorted(set(new) - set(ref))
    common = sorted(set(ref) & set(new))
    print(f'reference {len(ref)}, built {len(new)}, common {len(common)}')
    print(f'missing (reference only, {len(only_ref)}): ' + ' '.join(only_ref))
    print(f'extra (built only, {len(only_new)}): ' + ' '.join(only_new))
    vm_bad, ver = [], []
    for m in common:
        a, b = kmod.modinfo(ref[m]), kmod.modinfo(new[m])
        va, vb = kmod.first(a, 'vermagic'), kmod.first(b, 'vermagic')
        # Synology's own modules carry no vermagic at all
        if va and va != vb:
            vm_bad.append(f'{m}: "{va}" vs "{vb}"')
        xa, xb = kmod.first(a, 'version'), kmod.first(b, 'version')
        if xa != xb:
            ver.append(f'{m} {xa or "-"} -> {xb or "-"}')
    print(f'vermagic differs ({len(vm_bad)}): ' + ('; '.join(vm_bad) or '-'))
    print(f'version differs ({len(ver)}): ' + ('; '.join(ver) or '-'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
