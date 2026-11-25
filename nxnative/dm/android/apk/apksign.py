# apksign.py: the stored ZIP and the APK Signature Scheme v2 signer (RSA-2048, SHA-256, self-signed certificate) of mkapk.py, with the Python standard library only.
# mkapk.py builds an APK with them; apkdriver.py rebuilds an installed one with another Vulkan driver library
from contextlib import contextmanager
from pathlib import Path
import errno, hashlib, hmac, json, os, secrets, shutil, struct, tempfile, time, zlib

@contextmanager
def signing_lock(path):
    # Keep the lock inode in place. Both locks are automatically released if
    # a recompile is interrupted after generating the key or signing.
    with open(path, 'ab') as lock:
        if os.name == 'nt':
            import msvcrt
            while True:
                lock.seek(0)
                try:
                    msvcrt.locking(lock.fileno(), msvcrt.LK_LOCK, 1)
                    break
                except OSError as e:
                    if e.errno not in (errno.EACCES, errno.EDEADLK):
                        raise
            try:
                yield
            finally:
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX)
            yield

def write_signing_state(path, data):
    # A fault or process kill must not leave a partial signer or version file.
    fd, tmp = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='ascii') as out:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        os.replace(tmp, path)
    finally:
        Path(tmp).unlink(missing_ok=True)

def read_signer(path):
    record = json.loads(Path(path).read_text(encoding='ascii'))
    return {name: int(record[name]) for name in ('n', 'e', 'd')}, bytes.fromhex(record['cert'])


def version_directory(key_path):
    return Path(str(key_path) + '.versions')


def persistent_signer(path, derive=None):
    path = Path(path)
    with signing_lock(path.with_suffix(path.suffix + '.lock')):
        if path.exists():
            return read_signer(path)
        t = time.monotonic()
        if derive is not None:
            key, certificate = derive()
        else:
            if version_directory(path).exists():
                raise ValueError('Original APK signing key is missing. Restore it before recompiling this game.')
            key = gen_key()
            certificate = cert(key)
        write_signing_state(path, json.dumps({**{a: str(v) for a, v in key.items()}, 'cert': certificate.hex()}))
        print(f'new signing key {path} ({time.monotonic() - t:.1f} s)')
        return key, certificate

@contextmanager
def upgrade_version(key_path, package, requested):
    directory = version_directory(key_path)
    directory.mkdir(exist_ok=True)
    path = directory / hashlib.sha256(package.encode('utf-8')).hexdigest()
    with signing_lock(path.with_suffix('.lock')):
        previous = int(path.read_text(encoding='ascii')) if path.exists() else 0
        version = max(previous, requested)
        # Reserve a high-water mark before any bytes under this version are
        # published. A retry can still use exactly the same version.
        if version != previous:
            write_signing_state(path, f'{version}\n')
        yield version


# ---------------- RSA, DER, self-signed certificate ----------------
def is_prime(n, randbelow=secrets.randbelow):
    if n < 2: return False
    for p in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37):
        if n % p == 0: return n == p
    d, r = n - 1, 0
    while d % 2 == 0: d //= 2; r += 1
    for _ in range(40):
        x = pow(randbelow(n - 3) + 2, d, n)
        if x in (1, n - 1): continue
        for _ in range(r - 1):
            x = pow(x, 2, n)
            if x == n - 1: break
        else: return False
    return True
def gen_prime(bits, randbits=secrets.randbits, randbelow=secrets.randbelow):
    while True:
        p = randbits(bits) | (3 << (bits - 2)) | 1
        if p % 65537 != 1 and is_prime(p, randbelow): return p
def gen_key():
    p, q = gen_prime(1024), gen_prime(1024)
    n, e = p * q, 65537
    return {'n': n, 'e': e, 'd': pow(e, -1, (p - 1) * (q - 1))}

def derive_signer_v1(seed, title):
    """Reproduce the RSA key and exact certificate for a private seed and base title ID."""
    # The random stream, prime tests and DER bytes are part of the v1 signing identity.
    title_seed = hmac.digest(seed, b'nx2a standalone signing v1\0' + bytes.fromhex(title), 'sha256')
    counter = 0

    def randbits(bits):
        nonlocal counter
        counter += 1
        digest = hashlib.shake_256(title_seed + counter.to_bytes(8, 'big')).digest((bits + 7) // 8)
        return int.from_bytes(digest, 'big') & ((1 << bits) - 1)

    def randbelow(limit):
        while True:
            value = randbits(limit.bit_length())
            if value < limit:
                return value

    p = gen_prime(1024, randbits, randbelow)
    q = gen_prime(1024, randbits, randbelow)
    while abs(p - q) <= 1 << 924:
        q = gen_prime(1024, randbits, randbelow)
    e = 65537
    key = {'n': p * q, 'e': e, 'd': pow(e, -1, (p - 1) * (q - 1))}
    serial = int.from_bytes(hmac.digest(title_seed, b'certificate serial', 'sha256')[:8], 'big')
    return key, cert(key, serial=(serial >> 2) | 1)


def der(tag, body):
    l = len(body)
    ln = bytes([l]) if l < 0x80 else (lambda b: bytes([0x80 | len(b)]) + b)(l.to_bytes((l.bit_length() + 7) // 8, 'big'))
    return bytes([tag]) + ln + body
def der_int(v): return der(0x02, v.to_bytes(v.bit_length() // 8 + 1, 'big'))
def der_seq(*xs): return der(0x30, b''.join(xs))
def der_oid(s):
    a = [int(x) for x in s.split('.')]; out = bytes([a[0] * 40 + a[1]])
    for v in a[2:]:
        b = [v & 0x7f]; v >>= 7
        while v: b.append(0x80 | (v & 0x7f)); v >>= 7
        out += bytes(reversed(b))
    return der(0x06, out)
NULL = b'\x05\x00'
ALG_SHA256_RSA = der_seq(der_oid('1.2.840.113549.1.1.11'), NULL)
def spki(k): return der_seq(der_seq(der_oid('1.2.840.113549.1.1.1'), NULL), der(0x03, b'\0' + der_seq(der_int(k['n']), der_int(k['e']))))
def rsa_sign(k, msg):
    di = bytes.fromhex('3031300d060960864801650304020105000420') + hashlib.sha256(msg).digest()
    kl = (k['n'].bit_length() + 7) // 8
    em = b'\x00\x01' + b'\xff' * (kl - len(di) - 3) + b'\x00' + di
    return pow(int.from_bytes(em, 'big'), k['d'], k['n']).to_bytes(kl, 'big')
def cert(k, serial=None):
    if serial is None: serial = secrets.randbits(62) | 1
    name = der_seq(der(0x31, der_seq(der_oid('2.5.4.3'), der(0x0c, b'nx2a probe'))))
    validity = der_seq(der(0x17, b'260101000000Z'), der(0x18, b'20560101000000Z'))
    tbs = der_seq(der(0xa0, der_int(2)), der_int(serial), ALG_SHA256_RSA, name, validity, name, spki(k))
    return der_seq(tbs, ALG_SHA256_RSA, der(0x03, b'\0' + rsa_sign(k, tbs)))

# ---------------- ZIP (stored) ----------------
def build_zip(entries):   # entries: [(name, data, align)]
    out, cd = bytearray(), bytearray()
    dos_time, dos_date = 0, (2026 - 1980) << 9 | 1 << 5 | 1
    for name, data, align in entries:
        nb = name.encode(); crc = zlib.crc32(data)
        pad = -(len(out) + 30 + len(nb)) % align
        if 0 < pad < 4: pad += align   # too small for an extra field header: go to the next alignment step
        extra = struct.pack('<HH', 0xd935, pad - 4) + b'\0' * (pad - 4) if pad else b''   # 0xd935: zipalign's alignment extra field
        off = len(out)
        out += struct.pack('<IHHHHHIIIHH', 0x04034b50, 10, 0, 0, dos_time, dos_date, crc, len(data), len(data), len(nb), len(extra)) + nb + extra + data
        cd += struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 10, 10, 0, 0, dos_time, dos_date, crc, len(data), len(data), len(nb), 0, 0, 0, 0, 0, off) + nb
    eocd = struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(entries), len(entries), len(cd), len(out), 0)
    return bytes(out), bytes(cd), eocd

# ---------------- APK Signature Scheme v2 ----------------
def lp(b): return struct.pack('<I', len(b)) + b
def chunked_sha256(sections):
    digests = []
    for sec in sections:
        for i in range(0, len(sec), 1 << 20):
            c = sec[i:i + (1 << 20)]
            digests.append(hashlib.sha256(b'\xa5' + struct.pack('<I', len(c)) + c).digest())
    return hashlib.sha256(b'\x5a' + struct.pack('<I', len(digests)) + b''.join(digests)).digest()
def _v2_block(digest, k, crt):
    ALG = 0x0103   # RSASSA-PKCS1-v1_5 with SHA2-256
    signed = lp(lp(struct.pack('<I', ALG) + lp(digest))) + lp(lp(crt)) + lp(b'')
    signer = lp(signed) + lp(lp(struct.pack('<I', ALG) + lp(rsa_sign(k, signed)))) + lp(spki(k))
    value = lp(lp(signer))
    pair = struct.pack('<QI', 4 + len(value), 0x7109871a) + value
    size = len(pair) + 8 + 16
    return struct.pack('<Q', size) + pair + struct.pack('<Q', size) + b'APK Sig Block 42'


def sign_v2(zip_entries, cd, eocd, k, crt):
    # APK v2 hashes EOCD with the central-directory offset pointing to the start of the signing block,
    # not the on-disk EOCD's relocated central-directory offset.
    placeholder = _v2_block(bytes(32), k, crt)
    new_eocd = eocd[:16] + struct.pack('<I', len(zip_entries) + len(placeholder)) + eocd[20:]
    digest = chunked_sha256([zip_entries, cd, eocd])
    block = _v2_block(digest, k, crt)
    if len(block) != len(placeholder): raise ValueError('APK signing block changed size while signing')
    return zip_entries + block + cd + new_eocd

# ---------------- bounded-memory ZIP/APK path ----------------
# Large game packs are file-backed. Keep only the central directory and one 1 MiB
# I/O buffer resident; the pack, host library and APK are never read into RAM whole.
class ZipEntrySource:
    """A stored entry in an open source APK, copied with bounded memory."""
    def __init__(self, archive, info):
        self.archive, self.info = archive, info

    def open(self):
        return self.archive.open(self.info)


def build_zip_file(entries, path):
    cd = bytearray()
    count = 0
    with open(path, 'wb') as out:
        dos_time, dos_date = 0, (2026 - 1980) << 9 | 1 << 5 | 1
        for name, data, align in entries:
            if count >= 65535: raise ValueError('APK has too many ZIP entries for the current non-ZIP64 exporter')
            nb = name.encode()
            if len(nb) > 65535: raise ValueError(f'APK entry name is too long: {name}')
            if isinstance(data, (bytes, bytearray, memoryview)):
                size = len(data)
                crc = zlib.crc32(data)
                source = None
            elif isinstance(data, ZipEntrySource):
                source = data
                size, crc = data.info.file_size, data.info.CRC
            else:
                source = os.fspath(data)
                size = os.path.getsize(source)
                crc = 0
                with open(source, 'rb') as src:
                    while True:
                        block = src.read(1 << 20)
                        if not block: break
                        crc = zlib.crc32(block, crc)
            if size >= 0xffffffff or out.tell() >= 0xffffffff:
                raise ValueError('APK exceeds the classic ZIP 4 GiB limit; this exporter does not yet write ZIP64 APKs')
            pad = -(out.tell() + 30 + len(nb)) % align
            if 0 < pad < 4: pad += align
            extra = struct.pack('<HH', 0xd935, pad - 4) + b'\0' * (pad - 4) if pad else b''
            off = out.tell()
            out.write(struct.pack('<IHHHHHIIIHH', 0x04034b50, 10, 0, 0, dos_time, dos_date,
                                  crc, size, size, len(nb), len(extra)))
            out.write(nb); out.write(extra)
            if source is None:
                out.write(data)
            else:
                with (source.open() if isinstance(source, ZipEntrySource) else open(source, 'rb')) as src:
                    shutil.copyfileobj(src, out, 1 << 20)
            cd += struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 10, 10, 0, 0, dos_time, dos_date,
                              crc, size, size, len(nb), 0, 0, 0, 0, 0, off) + nb
            count += 1
            if out.tell() >= 0xffffffff: raise ValueError('APK exceeds the classic ZIP 4 GiB limit; ZIP64 APKs are not supported yet')
        cd_offset = out.tell()
        if len(cd) >= 0xffffffff: raise ValueError('APK central directory exceeds the classic ZIP limit')
        out.write(cd)
        eocd_offset = out.tell()
        eocd = struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, count, count, len(cd), cd_offset, 0)
        out.write(eocd)
        if out.tell() >= 0xffffffff: raise ValueError('APK exceeds the classic ZIP 4 GiB limit; ZIP64 APKs are not supported yet')
    return cd_offset, len(cd), eocd_offset, eocd


def sign_v2_file(zip_path, out_path, cd_offset, cd_size, eocd_offset, eocd, k, crt):
    if os.path.getsize(zip_path) != eocd_offset + len(eocd):
        raise OSError('unexpected APK staging ZIP length while signing')
    placeholder = _v2_block(bytes(32), k, crt)
    new_eocd = eocd[:16] + struct.pack('<I', cd_offset + len(placeholder)) + eocd[20:]
    digests = []
    with open(zip_path, 'rb') as src:
        for start, length in ((0, cd_offset), (cd_offset, cd_size)):
            src.seek(start)
            left = length
            while left:
                block = src.read(min(1 << 20, left))
                if not block: raise OSError('truncated APK staging ZIP while signing')
                digests.append(hashlib.sha256(b'\xa5' + struct.pack('<I', len(block)) + block).digest())
                left -= len(block)
    for start in range(0, len(eocd), 1 << 20):
        block = eocd[start:start + (1 << 20)]
        digests.append(hashlib.sha256(b'\xa5' + struct.pack('<I', len(block)) + block).digest())
    digest = hashlib.sha256(b'\x5a' + struct.pack('<I', len(digests)) + b''.join(digests)).digest()
    block = _v2_block(digest, k, crt)
    if len(block) != len(placeholder): raise ValueError('APK signing block changed size while signing')
    # The ZIP staging file is already in the output directory. Insert the signing block in place,
    # keeping only the central directory (small) in memory; avoid a second full-APK temporary copy.
    with open(zip_path, 'r+b') as apk:
        apk.seek(cd_offset)
        central_directory = apk.read(cd_size)
        if len(central_directory) != cd_size: raise OSError('truncated APK central directory while signing')
        apk.seek(cd_offset)
        apk.truncate(cd_offset)
        apk.write(block)
        apk.write(central_directory)
        apk.write(new_eocd)
        apk.flush()
        os.fsync(apk.fileno())
    if os.path.getsize(zip_path) >= 0xffffffff:
        raise ValueError('signed APK exceeds the classic ZIP 4 GiB limit; ZIP64 APKs are not supported yet')
    os.replace(zip_path, out_path)
    return os.path.getsize(out_path)

class _LimitedReader:
    def __init__(self, stream, remaining):
        self.stream, self.remaining = stream, remaining
    def read(self, size=-1):
        if self.remaining <= 0: return b''
        if size < 0 or size > self.remaining: size = self.remaining
        data = self.stream.read(size)
        self.remaining -= len(data)
        return data
