"""Small helpers to read a kernel module (.ko) without kmod: its .modinfo
fields, and its defined/undefined/exported symbols via the toolchain's nm."""
import struct
import subprocess


def section(path, name):
    """Return the raw bytes of an ELF64 little-endian section, or b''."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'\x7fELF' or data[4] != 2:
        return b''
    shoff, = struct.unpack_from('<Q', data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x3A)
    def sh(i):
        return struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize)
    stroff = sh(shstrndx)[4]
    for i in range(shnum):
        hdr = sh(i)
        end = data.index(b'\0', stroff + hdr[0])
        if data[stroff + hdr[0]:end].decode() == name:
            return data[hdr[4]:hdr[4] + hdr[5]]
    return b''


def modinfo(path):
    """.modinfo as a dict of lists (a key such as alias or parm repeats)."""
    info = {}
    for item in section(path, '.modinfo').split(b'\0'):
        if b'=' in item:
            k, v = item.split(b'=', 1)
            info.setdefault(k.decode(errors='replace'), []).append(v.decode(errors='replace'))
    return info


def first(info, key, default=''):
    v = info.get(key)
    return v[0] if v else default


def symbols(path, nm='nm'):
    """(undefined, exported) symbol name sets."""
    out = subprocess.run([nm, path], capture_output=True, text=True, check=True).stdout
    undef, exported = set(), set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] == 'U':
            undef.add(parts[1])
        elif len(parts) == 3:
            # EXPORT_SYMBOL{,_GPL}(x) defines __ksymtab_x in both 4.4 and
            # 5.10 (the GPL-ness is in the section name, not the symbol).
            if parts[2].startswith('__ksymtab_'):
                exported.add(parts[2][len('__ksymtab_'):])
    return undef, exported
