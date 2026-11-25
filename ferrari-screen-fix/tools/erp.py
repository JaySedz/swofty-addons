# Lists / extracts resources from an EGO ERPK (v3/v4) package.
# Usage: erp.py <file.erp> [substring] [outdir]   (outdir: write each matching fragment, decompressed)
import ctypes, os, struct, sys, zlib

def _find_zstd():
    # any 64-bit libzstd.dll: ZSTD_DLL, else the one GitHub Desktop's bundled Git ships
    if os.environ.get('ZSTD_DLL'):
        return os.path.dirname(os.environ['ZSTD_DLL'])
    import glob
    hits = sorted(glob.glob(os.path.join(os.environ.get('LOCALAPPDATA', ''), 'GitHubDesktop', 'app-*', 'resources', 'app',
                                         'git', 'ucrt64', 'bin', 'libzstd.dll')))
    if not hits:
        raise SystemExit('libzstd.dll not found: set ZSTD_DLL to a 64-bit libzstd.dll')
    return os.path.dirname(hits[-1])


_ZDIR = _find_zstd()
os.add_dll_directory(_ZDIR)
_z = ctypes.CDLL(os.path.join(_ZDIR, 'libzstd.dll'))
_z.ZSTD_decompress.restype = ctypes.c_size_t
_z.ZSTD_compress.restype = ctypes.c_size_t
_z.ZSTD_compressBound.restype = ctypes.c_size_t


def unzstd(b, n):
    out = ctypes.create_string_buffer(n)
    r = _z.ZSTD_decompress(out, ctypes.c_size_t(n), b, ctypes.c_size_t(len(b)))
    assert r == n, (r, n)
    return out.raw


def zstd(b, level=19):
    cap = _z.ZSTD_compressBound(ctypes.c_size_t(len(b)))
    out = ctypes.create_string_buffer(cap)
    r = _z.ZSTD_compress(out, ctypes.c_size_t(cap), b, ctypes.c_size_t(len(b)), level)
    return out.raw[:r]


def entries(f):
    assert f[:4] == b'ERPK'
    ver, = struct.unpack_from('<I', f, 4)
    data_off, = struct.unpack_from('<Q', f, 0x20)
    nfiles, = struct.unpack_from('<I', f, 0x30)
    p = 0x38
    for _ in range(nfiles):
        elen, = struct.unpack_from('<I', f, p)
        start = p + 4
        idlen, = struct.unpack_from('<H', f, start)
        name = f[start + 2:start + 2 + idlen].split(b'\0')[0].decode('latin1')
        q = start + 2 + idlen
        rtype = f[q:q + 16].split(b'\0')[0].decode('latin1')
        q += 16
        q += 4
        nfrag = f[q]
        q += 1
        frags = []
        for _k in range(nfrag):
            fn = f[q:q + 4].decode('latin1')
            q += 4
            off, size, flags = struct.unpack_from('<QQI', f, q)
            q += 20
            comp, packed = 0, size
            if ver > 2:
                comp = f[q]
                packed, = struct.unpack_from('<Q', f, q + 1)
                q += 9
            frags.append((fn, data_off + off, size, flags, comp, packed))
        p = start + elen
        yield name, rtype, frags


def fragment(f, fr):
    fn, off, size, flags, comp, packed = fr
    raw = f[off:off + packed]
    if comp == 1:
        return zlib.decompress(raw)
    if comp == 17:
        return unzstd(raw, size)
    return raw  # 0 / 0x91: stored


if __name__ == '__main__':
    f = open(sys.argv[1], 'rb').read()
    want = sys.argv[2] if len(sys.argv) > 2 else None
    out = sys.argv[3] if len(sys.argv) > 3 else None
    for name, rtype, frags in entries(f):
        if want and want not in name:
            continue
        print(name, rtype, [(a, c, e) for a, b, c, d, e, g in frags])
        if out:
            safe = name.replace('eaid://', '').replace('/', '~').replace('?', '@').replace('&', '+').replace('=', '-')
            for fr in frags:
                open(os.path.join(out, f'{safe}.{fr[0]}'), 'wb').write(fragment(f, fr))
