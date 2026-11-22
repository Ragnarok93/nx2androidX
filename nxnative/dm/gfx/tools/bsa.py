"""Streaming Bethesda BSA v103/104/105 archive member reader.

Reads only requested file payloads, rather than mapping entire multi-GB
archives. Used by the shader splitter for Bethesda-engine shader sources.
"""
import os
import struct
import zlib

MAX_MEMBER = 128 << 20
MAX_TABLE = 64 << 20


def _exact(f, n):
    data = f.read(n)
    if len(data) != n:
        raise ValueError('truncated BSA archive')
    return data


def members(path, wanted=lambda name: True):
    """Yield (member path, uncompressed bytes) for selected BSA entries."""
    with open(path, 'rb') as f:
        size = os.fstat(f.fileno()).st_size
        if size < 36:
            raise ValueError('truncated BSA header')
        header = _exact(f, 36)
        if header[:4] != b'BSA\0':
            raise ValueError('not a Bethesda BSA archive')
        ver, start, flags, ndirs, nfiles, dirnames, filenames, _types = struct.unpack_from('<8I', header, 4)
        if ver not in (103, 104, 105):
            raise ValueError(f'unsupported BSA version {ver}')
        recsize = 24 if ver == 105 else 16
        if (start < 36 or start > size or ndirs > nfiles or nfiles > 2_000_000 or
                recsize * ndirs + dirnames + filenames + nfiles * 16 > MAX_TABLE):
            raise ValueError('invalid or oversized BSA directory')
        if (flags & 3) != 3:
            raise ValueError('BSA requires folder and file name tables')
        f.seek(start)
        dirs = _exact(f, recsize * ndirs)
        counts = [struct.unpack_from('<I', dirs, i * recsize + 8)[0] for i in range(ndirs)]
        if sum(counts) != nfiles:
            raise ValueError('BSA folder file count mismatch')
        entries = []
        for n in counts:
            length = _exact(f, 1)[0]
            folder_name = _exact(f, length)
            if not folder_name or folder_name[-1] != 0:
                raise ValueError('BSA folder name is not NUL terminated')
            folder = folder_name[:-1].decode('utf-8', 'replace').replace('\\', '/')
            records = _exact(f, n * 16)
            for i in range(n):
                packed, off = struct.unpack_from('<II', records, i * 16 + 8)
                entries.append((folder, packed, off))
        names = _exact(f, filenames)
        name_iter = iter(names.split(b'\0'))
        for folder, packed, off in entries:
            name = next(name_iter, b'')
            if not name:
                raise ValueError('missing BSA file name')
            name = name.decode('utf-8', 'replace')
            full = (folder + '/' if folder else '') + name
            if not wanted(full):
                continue
            stored = packed & 0x3fffffff
            compressed = bool(flags & 4) ^ bool(packed & 0x40000000)
            if not stored or stored > MAX_MEMBER or off > size or stored > size - off:
                raise ValueError(f'{full}: invalid BSA member size or offset')
            f.seek(off)
            data = _exact(f, stored)
            if ver >= 104 and flags & 0x100:
                skip = 1 + data[0]
                if skip >= len(data):
                    raise ValueError(f'{full}: invalid embedded BSA filename')
                data = data[skip:]
            if compressed:
                if len(data) < 4:
                    raise ValueError(f'{full}: truncated BSA compression header')
                expected = struct.unpack_from('<I', data)[0]
                if expected > MAX_MEMBER:
                    raise ValueError(f'{full}: oversized decompressed BSA member')
                if ver == 105:
                    import lz4.frame
                    data = lz4.frame.decompress(data[4:])
                else:
                    data = zlib.decompress(data[4:])
                if len(data) != expected:
                    raise ValueError(f'{full}: incorrect decompressed BSA size')
            yield full, data
