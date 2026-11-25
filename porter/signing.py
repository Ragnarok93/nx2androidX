"""Select a private signing key for standalone game APKs."""
import hashlib
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


def export_key(title):
    legacy = paths.signing_key()
    key = title_key(legacy, title)
    # Older porters could export without recording a title or a version floor.
    # Preserve their signer even when the previous APK cannot be located.
    if not (key.exists() or key.with_suffix('.signer-v1').exists() or version_directory(key).exists()):
        if legacy.exists() or version_directory(legacy).exists():
            return legacy
    try:
        return provision_title_key(legacy, key)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise BuildError(f'Cannot prepare the standalone signing key: {exc}') from exc


def original_key(apk, *, title=None, explicit=None):
    """Find an existing private key for the APK's certificate and public key."""
    if explicit is not None:
        candidates = [Path(explicit)]
    else:
        legacy = paths.signing_key()
        if title is None:
            directory = legacy.with_suffix('.titles')
            candidates = [legacy, *sorted(set(directory.glob('*.json')) |
                                          {pin.with_suffix('.json') for pin in directory.glob('*.signer-v1')})]
        else:
            candidates = [legacy, title_key(legacy, title)]
    try:
        with zipfile.ZipFile(apk) as archive:
            identity = source_certificate(apk, archive.start_dir)
    except (OSError, ValueError, struct.error, zipfile.BadZipFile) as exc:
        raise BuildError(f'Cannot read the original APK signing identity: {exc}') from exc
    fingerprint = hashlib.sha256(identity[0]).hexdigest()
    for path in candidates:
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
                provision_title_key(legacy, path, expected=fingerprint)
            key, certificate = read_signer(path)
            if identity == (certificate, spki(key)):
                return path
        except (OSError, ValueError, KeyError, TypeError):
            continue
    raise BuildError('Original APK signing key is unavailable or does not match. Restore the original key to upgrade in place.')
