"""Unpublished companion RomFS staging for Android split exports."""
import json
import shutil
from pathlib import Path

RECEIPT = '.nx2a-data.json.previous'
MANIFEST = 'nx2a-data.json'
PENDING = 'extracting-v1'
READY = 'extracted-v1'


def _receipt(dst):
    dst = Path(dst)
    receipt = dst / RECEIPT
    if (dst.is_symlink() or not dst.is_dir() or receipt.is_symlink()
            or (dst / MANIFEST).exists() or (dst / MANIFEST).is_symlink()
            or (dst / 'RomFS').is_symlink()):
        return None
    try:
        data = json.loads(receipt.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return None
    return data if isinstance(data, dict) else None


def _matches(data, identity, state):
    return bool(data and data.get('direct_romfs') == state
                and all(data.get(k) == identity[k] for k in ('title_id', 'package', 'version')))


def _write(dst, identity, state):
    receipt = dst / RECEIPT
    pending = dst / (RECEIPT + '.new')
    pending.unlink(missing_ok=True)
    with pending.open('x', encoding='utf-8') as f:
        json.dump(dict(identity, direct_romfs=state), f)
    pending.replace(receipt)


def ready_receipt(dst):
    dst = Path(dst)
    data = _receipt(dst)
    if (data and data.get('direct_romfs') == READY and (dst / 'RomFS').is_dir()):
        return data
    return None


def _owned_pending(dst, identity):
    return (_matches(_receipt(dst), identity, PENDING)
            and {p.name for p in dst.iterdir()} <= {RECEIPT, RECEIPT + '.new', 'RomFS'})


def begin(dst, identity):
    """Create an unpublished pack, or restart only our own incomplete extraction."""
    dst = Path(dst)
    if dst.is_symlink():
        return None
    if dst.exists():
        if not _owned_pending(dst, identity):
            return None
        rom = dst / 'RomFS'
        if rom.exists():
            if not rom.is_dir():
                return None
            shutil.rmtree(rom)
    else:
        dst.mkdir(parents=True)
    _write(dst, identity, PENDING)
    rom = dst / 'RomFS'
    rom.mkdir()
    return rom


def finish(dst, identity):
    """Mark the extraction complete; normal data packing publishes the manifest."""
    dst = Path(dst)
    if not _owned_pending(dst, identity) or not (dst / 'RomFS').is_dir():
        raise OSError(f'cannot mark unowned RomFS staging as complete: {dst}')
    _write(dst, identity, READY)


def discard_pending(dst, identity):
    dst = Path(dst)
    if _owned_pending(dst, identity):
        shutil.rmtree(dst)
