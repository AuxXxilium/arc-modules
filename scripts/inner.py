#!/usr/bin/env python3
"""inner.py <container> <member-pattern>[!<member-pattern>...] <out>: copy
one member of a zip, rpm (source RPM) or tar archive to <out>; with "!",
a member of that member, and so on.

Vendors ship drivers inside a zip (Broadcom, Marvell) or a source RPM
(Marvell FastLinQ, Mellanox OFED); the pinned checksum covers the container,
and the source tarball inside it is what gets built. <member-pattern> is an
fnmatch pattern on the member's path; exactly one member must match.
Standard library only."""
import bz2
import fnmatch
import gzip
import lzma
import os
import shutil
import struct
import sys
import tarfile
import zipfile


def pick(names, pattern):
    hits = [n for n in names if fnmatch.fnmatch(n, pattern) or fnmatch.fnmatch(os.path.basename(n), pattern)]
    if len(hits) != 1:
        sys.exit(f'inner.py: {len(hits)} members match {pattern!r}: {hits[:5]}')
    return hits[0]


def rpm_payload(path):
    """The decompressed cpio payload of an RPM."""
    f = open(path, 'rb')
    lead = f.read(96)
    if lead[:4] != b'\xed\xab\xee\xdb':
        sys.exit('inner.py: not an RPM')

    def header(align):
        magic = f.read(8)
        if magic[:3] != b'\x8e\xad\xe8':
            sys.exit('inner.py: bad RPM header')
        nindex, hsize = struct.unpack('>II', f.read(8))
        f.read(16 * nindex + hsize)
        if align:
            pad = (8 - (16 + 16 * nindex + hsize) % 8) % 8
            f.read(pad)

    header(True)    # signature, padded to 8 bytes
    header(False)   # the package header
    data = f.read()
    if data[:2] == b'\x1f\x8b':
        return gzip.decompress(data)
    if data[:6] == b'\xfd7zXZ\x00':
        return lzma.decompress(data)
    if data[:3] == b'BZh':
        return bz2.decompress(data)
    sys.exit('inner.py: unsupported RPM payload compression')


def cpio_members(blob):
    """(name, data) of a newc cpio archive."""
    off = 0
    while off + 110 <= len(blob):
        hdr = blob[off:off + 110]
        if hdr[:6] not in (b'070701', b'070702'):
            sys.exit('inner.py: bad cpio header')
        fields = [int(hdr[6 + 8 * i:14 + 8 * i], 16) for i in range(13)]
        fsize, nsize = fields[6], fields[11]
        name = blob[off + 110:off + 110 + nsize - 1].decode()
        off = (off + 110 + nsize + 3) & ~3
        data = blob[off:off + fsize]
        off = (off + fsize + 3) & ~3
        if name == 'TRAILER!!!':
            return
        yield name.lstrip('./'), data


def extract(src, pattern, out):
    if zipfile.is_zipfile(src):
        with zipfile.ZipFile(src) as z:
            m = pick([i.filename for i in z.infolist() if not i.is_dir()], pattern)
            with z.open(m) as i, open(out, 'wb') as o:
                shutil.copyfileobj(i, o)
    elif open(src, 'rb').read(4) == b'\xed\xab\xee\xdb':
        members = dict(cpio_members(rpm_payload(src)))
        m = pick(list(members), pattern)
        open(out, 'wb').write(members[m])
    else:
        with tarfile.open(src) as t:
            m = pick([i.name for i in t.getmembers() if i.isfile()], pattern)
            with t.extractfile(m) as i, open(out, 'wb') as o:
                shutil.copyfileobj(i, o)
    return m


def main():
    # "a!b": member b of member a (an archive inside the archive)
    src, patterns, out = sys.argv[1], sys.argv[2].split('!'), sys.argv[3]
    cur = src
    for n, pattern in enumerate(patterns):
        dst = out if n == len(patterns) - 1 else f'{out}.{n}'
        print(extract(cur, pattern, dst))
        if cur != src:
            os.remove(cur)
        cur = dst


if __name__ == '__main__':
    main()
