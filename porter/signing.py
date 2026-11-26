"""Select a private signing key for standalone game APKs."""
import hashlib
import json
import os
import re
import secrets
import struct
import sys
import zipfile
from pathlib import Path

from porter import paths
from porter.upstream import BuildError

# Use the same APK signing block reader and key format as the standalone tools.
_apk_tools = str(paths.DM / 'android' / 'apk')
if _apk_tools not in sys.path:
    sys.path.insert(0, _apk_tools)
from apkdriver import source_certificate
from apksign import (derive_signer_v1, persistent_signer, read_signer, signing_lock,
                     spki, version_directory, write_signing_state)


def title_key(legacy, title):
    if not re.fullmatch(r'[0-9a-fA-F]{16}', title):
        raise BuildError('A valid base title ID is required to select an APK signing key.')
    return legacy.with_suffix('.titles') / (title.upper() + '.json')


def signing_seed(legacy, *, create):
    path = legacy.with_suffix('.seed-v1')
    with signing_lock(path.with_suffix('.seed-v1.lock')):
        if not path.exists():
            if not create or any(legacy.with_suffix('.titles').glob('*.signer-v1')):
                raise ValueError('The original standalone signing seed is missing. Restore it to recover game keys.')
            write_signing_state(path, secrets.token_hex(32) + '\n')
        seed = bytes.fromhex(path.read_text(encoding='ascii'))
        if len(seed) != 32:
            raise ValueError('Invalid standalone signing seed.')
        return seed


def provision_title_key(legacy, key, *, expected=None):
    pin = key.with_suffix('.signer-v1')

    def derive():
        if expected is None and not pin.exists() and version_directory(key).exists():
            raise ValueError('Original APK signing key is missing. Restore it before recompiling this game.')
        seed = signing_seed(legacy, create=expected is None)
        private, certificate = derive_signer_v1(seed, key.stem)
        fingerprint = hashlib.sha256(certificate).hexdigest()
        if expected is not None and expected != fingerprint:
            raise ValueError('The standalone signing seed does not match the original APK.')
        if pin.exists():
            if pin.read_text(encoding='ascii').strip() != fingerprint:
                raise ValueError('The standalone signing seed does not match this title. Restore the original seed.')
        else:
            write_signing_state(pin, fingerprint + '\n')
        return private, certificate

    key.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    persistent_signer(key, derive=derive)
    return key


def package_receipt(key, package):
    return version_directory(key) / hashlib.sha256(package.encode('utf-8')).hexdigest()


def imported_keys(key):
    return sorted(key.parent.glob(key.stem + '-*.json'))


def signing_roots(primary):
    # Android's environment migration keeps conflicting old signer families
    # together; incomplete recovery staging directories are never considered.
    recovery = primary.with_suffix('.recovery')
    return [primary, *(d / 'signing-key.json' for d in sorted(recovery.glob('*'))
                       if d.is_dir() and not d.name.startswith('.'))]


def known_title_key(key):
    return key.exists() or key.with_suffix('.signer-v1').exists() or version_directory(key).exists()


def export_key(title, *, package=None, previous_apk=None):
    primary = paths.signing_key()
    key = title_key(primary, title)
    roots = signing_roots(primary)
    # A matching installed APK takes precedence over an exported copy, even
    # if the latter has a newer but incompatible signing identity.
    installed = json.loads(os.environ.get('NX2A_INSTALLED_GAME_APKS', '{}')).get(package) if package else None
    prior = Path(installed) if installed else Path(previous_apk) if previous_apk is not None else None
    if installed and not prior.is_file():
        raise BuildError('The installed game APK is no longer readable. Refresh installed games and retry.')
    if prior is not None and prior.is_file():
        if package:
            try:
                with zipfile.ZipFile(prior) as z:
                    if package.encode('utf-16le') + b'\0\0' not in z.read('AndroidManifest.xml'):
                        raise ValueError('The existing APK has a different package.')
            except (OSError, ValueError, KeyError, zipfile.BadZipFile) as exc:
                raise BuildError(f'Cannot verify the previous APK: {exc}') from exc
        return original_key(prior, title=title)

    title_records = [(root, title_key(root, title)) for root in roots]
    if package:
        receipts = [(root, p) for root, k in title_records
                    for p in (k, *imported_keys(k), root) if package_receipt(p, package).is_file()]
        if len(receipts) > 1:
            raise BuildError('Multiple signing histories exist for this package. Select the original APK to upgrade.')
        if receipts:
            root, p = receipts[0]
            if p == title_key(root, title):
                try:
                    return provision_title_key(root, p)
                except (OSError, ValueError, KeyError, TypeError) as exc:
                    raise BuildError(f'Cannot prepare the standalone signing key: {exc}') from exc
            if not p.is_file():
                raise BuildError('Original APK signing key is missing. Restore it before recompiling this game.')
            return p

    known = [(root, k) for root, k in title_records if known_title_key(k)]
    if len(known) > 1:
        raise BuildError('Multiple signing identities exist for this title. Select the original APK to upgrade.')
    if known:
        root, key = known[0]
    else:
        recovered = [p for _, k in title_records for p in imported_keys(k)]
        if len(recovered) > 1:
            raise BuildError('Multiple recovered keys exist for this title. Select the original APK to upgrade.')
        if recovered:
            return recovered[0]
        root = primary
    try:
        return provision_title_key(root, key)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise BuildError(f'Cannot prepare the standalone signing key: {exc}') from exc


def remember_imported_key(title, fingerprint, identity, private):
    # Imported legacy keys may differ from the current derived title signer.
    # Keep them separately, scoped to the title and verified certificate.
    key = title_key(paths.signing_key(), title)
    key.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    pin = key.with_suffix('.signer-v1')
    recovered = key if (not key.exists() and pin.is_file() and
                        pin.read_text(encoding='ascii').strip() == fingerprint) else key.with_name(key.stem + '-' + fingerprint + '.json')
    saved, certificate = persistent_signer(recovered, derive=lambda: (private, identity[0]))
    if identity != (certificate, spki(saved)):
        raise ValueError('The recovered signing key conflicts with an existing key.')
    return recovered


def original_key(apk, *, title=None, explicit=None):
    """Find an existing private key for the APK's certificate and public key."""
    primary = paths.signing_key()
    if explicit is not None:
        candidates = [(primary, Path(explicit))]
    else:
        candidates = []
        for root in signing_roots(primary):
            if title is None:
                directory = root.with_suffix('.titles')
                keys = sorted(set(directory.glob('*.json')) |
                              {pin.with_suffix('.json') for pin in directory.glob('*.signer-v1')})
            else:
                key = title_key(root, title)
                keys = [key, *imported_keys(key)]
            candidates += [(root, p) for p in (*keys, root)]
        # Check real keys first; recovery derivation can be expensive.
        candidates.sort(key=lambda item: not item[1].is_file())
    try:
        with zipfile.ZipFile(apk) as archive:
            identity = source_certificate(apk, archive.start_dir)
    except (OSError, ValueError, struct.error, zipfile.BadZipFile) as exc:
        raise BuildError(f'Cannot read the original APK signing identity: {exc}') from exc
    fingerprint = hashlib.sha256(identity[0]).hexdigest()
    for legacy, path in candidates:
        try:
            if not path.is_file():
                if explicit is not None or path == legacy:
                    continue
                pin = path.with_suffix('.signer-v1')
                if pin.is_file():
                    if pin.read_text(encoding='ascii').strip() != fingerprint:
                        continue
                elif title is None or not legacy.with_suffix('.seed-v1').is_file():
                    continue
                if not re.fullmatch(r'[0-9A-F]{16}', path.stem):
                    continue
                provision_title_key(legacy, path, expected=fingerprint)
            if path.stat().st_size > 65536:
                continue
            key, certificate = read_signer(path)
            if identity == (certificate, spki(key)):
                if explicit is not None and title is not None:
                    # Reject a damaged private exponent before saving it.
                    if key['n'].bit_length() != 2048 or not 1 < key['e'] < 2**32 or not 1 < key['d'] < key['n']:
                        continue
                    if pow(pow(2, key['d'], key['n']), key['e'], key['n']) != 2:
                        continue
                    return remember_imported_key(title, fingerprint, identity, key)
                return path
        except (OSError, ValueError, KeyError, TypeError):
            continue
    raise BuildError('Original APK signing key is unavailable or does not match. Restore the original key or private signing seed to upgrade in place. Certificate SHA-256: ' + fingerprint)
