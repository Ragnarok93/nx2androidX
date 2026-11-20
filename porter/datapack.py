# The data pack: everything the app reads from its files folder on the device (/sdcard/Android/data/<package>/files, front_player.c), as a folder beside the APK:
#   mods/          the rewritten code modules (patch_nso.py's outputs)
#   RomFS/         the game's files (the packed version's merged RomFS); the app's mode leaves it out: the pack points at the build folder's own RomFS (the manifest's romfs_at)
#   aoc/<index>/   the DLC the player has
#   sysdata/<title ID>/  system data from the player's firmware dump that the game reads (android.tsv sysdata, porter --firmware; env.txt's DM_SYSDATA)
#   shaders.dmsv   the Vulkan shaders (dm/android/tools/shader_vk.py)
#   nintendo_ext.ttf  the button-icon font, the stand-in for the Nintendo extension shared font (dm/tools/make_ext_font.py; env.txt's DM_SHARED_FONT_EXT)
#   standard.ttf   the player's font for the system's shared fonts, with --shared-font (env.txt's DM_SHARED_FONT)
#   env.txt        host switches: the game language, the DLC folder, the profile's env rows, then the port's (--fps60, --mt-replay); on the device the installer adds its
#                  DM_ROMFS row after them when the app reads its RomFS from another folder (porter/install.py, --romfs-at): that is a fact of one device, not of the pack
#   settings.txt   the pause menu's first-launch defaults (the profile's setting rows, then the port's: --touch); the installer never replaces the player's own
#   nx2a-data.json what the pack is: the game, the package, the version, the device features the profile requires (android.tsv requires), which the installer checks
# Big trees use hard links when possible (relative symbolic links for denied private hard links), avoiding extra copies; the pack is rebuilt when changed.
import json, os, shutil, time
from pathlib import Path
from porter.revision import PLAYER_API_REVISION

MANIFEST = 'nx2a-data.json'
KEEP_ON_DEVICE = ('settings.txt',)          # the player's choices win over the pack's defaults
OWNED = ('mods', 'RomFS', 'aoc', 'sysdata')            # folders the pack owns on the device: files there that the pack lacks are stale (an older version's) and removed
ROMFS = 'RomFS'


def link_or_copy(src, dst, require_links=False):
    dst.parent.mkdir(parents=True, exist_ok=True)
    try: os.link(src, dst)
    except OSError as e:
        if require_links:
            try:
                # Android can deny hard links even inside private storage. A relative
                # symlink stays valid when embed-pack.partial becomes embed-pack, and
                # ZIP creation later archives the target bytes, not the symlink.
                os.symlink(os.path.relpath(src, dst.parent), dst)
            except OSError as symlink_error:
                raise OSError(f'cannot hard-link or create a symbolic link for {src} in private APK staging: '
                              f'hard-link: {e}; symbolic link: {symlink_error}') from symlink_error
            return
        shutil.copy2(src, dst)


def link_tree(src, dst, require_links=False):   # (files, bytes)
    n = b = 0
    for root, _, files in os.walk(src):
        for f in files:
            s = Path(root) / f
            link_or_copy(s, dst / s.relative_to(src), require_links=require_links); n += 1; b += s.stat().st_size
    return n, b


def embedded_files(pack):
    """Stream actual file bytes to the standalone APK, including hard-link fallback targets."""
    for path in sorted(pack.rglob('*')):
        if path.is_file():
            yield path


def write_embedded_zip(pack, archive, byte_limit):
    """Write the standalone game's data ZIP without exceeding the APK's classic ZIP budget.

    Android's installer needs a classic ZIP APK with a v2 signing block, so the embedded
    asset must leave space for the host, drivers, metadata and signing block. The *inner*
    ZIP may use ZIP64 (large RomFS files), but must itself fit as one APK asset.
    First try fast DEFLATE for small packs; use level 9 when space is tight. Both
    levels are readable by Android's java.util.zip.ZipInputStream.
    """
    import zipfile

    if byte_limit <= 0:
        raise ValueError('there is no space for the embedded game archive in an Android APK')
    files = tuple(embedded_files(pack))
    raw_bytes = sum(path.stat().st_size for path in files)
    levels = (9,) if raw_bytes >= byte_limit else (1, 9)
    last_size = 0
    for level in levels:
        archive.unlink(missing_ok=True)
        overflow = False
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED,
                             compresslevel=level, allowZip64=True) as zf:
            for path in files:
                # Stream within every file, too: Unreal/IoStore archives may be
                # several gigabytes each. zf.write would finish the whole archive
                # before checking the output budget and could exhaust storage.
                info = zipfile.ZipInfo(path.relative_to(pack).as_posix())
                info.compress_type = zipfile.ZIP_DEFLATED
                info._compresslevel = level
                info.file_size = path.stat().st_size
                with path.open('rb') as src, zf.open(info, 'w') as dst:
                    while block := src.read(1 << 20):
                        dst.write(block)
                        if zf.fp.tell() >= byte_limit:
                            overflow = True
                            break
                if overflow:
                    break
        last_size = archive.stat().st_size
        if not overflow and last_size < byte_limit:
            return last_size, level
    raise ValueError(
        f'even maximum compatible ZIP compression cannot fit this game in one '
        f'installable APK (data alone is at least {last_size / 2**30:.2f} GiB; '
        f'available APK data budget {byte_limit / 2**30:.2f} GiB). '
        'Android APK ZIP64 is not supported; keep the compiled port cache for another export format'
    )


def files_folder(package): return f'/sdcard/Android/data/{package}/files'


def romfs_sizes(d):   # {path in the RomFS: size} of the pack in d, '/'-separated as the device lists them (install --romfs-at compares a folder on the device with it)
    r = Path(d) / ROMFS
    return {p.relative_to(r).as_posix(): p.stat().st_size for p in r.rglob('*') if p.is_file()} if r.is_dir() else {}


def reusable_companion(dst, expected, metadata, payload):
    previous = read_manifest(dst)
    if not isinstance(previous, dict) or dst.is_symlink():
        return False
    if {k: v for k, v in previous.items() if k != 'made'} != {k: v for k, v in expected.items() if k != 'made'}:
        return False
    assets = dict(payload)
    assets.update((p.relative_to(metadata).as_posix(), p) for p in metadata.rglob('*') if p.is_file())
    try:
        actual = set()
        for root, dirs, files in os.walk(dst):
            if Path(root) == dst:
                dirs[:] = [d for d in dirs if d != 'saves']
            if any((Path(root) / d).is_symlink() for d in dirs):
                return False
            actual.update((Path(root) / f).relative_to(dst).as_posix() for f in files)
        if actual != set(assets) | {MANIFEST}:
            return False
        for name, source in assets.items():
            existing = dst / name
            if existing.is_symlink() or existing.stat().st_size != source.stat().st_size:
                return False
            if existing.samefile(source):
                # A shared inode can have changed since the previous export.
                # In that case give the rebuilt pack a fresh manifest.
                return False
            with existing.open('rb') as old, source.open('rb') as new:
                while block := new.read(1024 * 1024):
                    if old.read(len(block)) != block:
                        return False
    except OSError:
        return False
    return True



def _same_payload_file(source, existing):
    """Verify bytes before reusing a prior export; never trust size alone."""
    source, existing = Path(source), Path(existing)
    if existing.is_symlink() or not existing.is_file():
        return False
    if source.samefile(existing):
        return True
    if source.stat().st_size != existing.stat().st_size:
        return False
    with source.open('rb') as new, existing.open('rb') as old:
        while block := new.read(1 << 20):
            if old.read(len(block)) != block:
                return False
        return not old.read(1)


def refresh_in_place(dst, staging, payload, generated_files, manifest, progress=None):
    """Update only modified assets in a previous pack, without a second full RomFS.

    A moved-aside manifest makes an interrupted refresh unimportable; the next
    build can resume from its saved identity and verify each file again. Saves
    and unrelated player files are never touched.
    """
    dst = Path(dst)
    current = dst / MANIFEST
    previous = dst / ('.' + MANIFEST + '.previous')
    if dst.is_symlink() or not dst.is_dir() or current.is_symlink() or previous.is_symlink():
        return False
    receipt = current if current.is_file() else previous
    try:
        old = json.loads(receipt.read_text(encoding='utf-8'))
    except (OSError, ValueError, TypeError):
        return False
    if not isinstance(old, dict) or any(old.get(k) != manifest[k] for k in ('title_id', 'package')):
        return False
    if progress:
        progress('reusing prior companion assets in place')

    # Move the published receipt aside first: an interrupted build must not
    # present a mixed-version pack as complete, and retries can still resume.
    if current.is_file():
        current.replace(previous)
    desired = set(payload)
    metadata = {p.relative_to(staging).as_posix(): p for p in staging.rglob('*') if p.is_file()}
    desired.update(metadata)
    desired.add(MANIFEST)

    def publish(name, source, direct_copy):
        rel = Path(name)
        if rel.is_absolute() or '..' in rel.parts or not rel.parts:
            raise ValueError(f'unsafe data pack asset name: {name}')
        target = dst / rel
        parent = dst
        for part in rel.parts[:-1]:
            parent = parent / part
            if parent.is_symlink():
                raise OSError(f'unsafe symlink directory in data pack: {parent}')
        if _same_payload_file(source, target):
            return
        parent.mkdir(parents=True, exist_ok=True)
        pending = target.with_name(target.name + '.nx2a.partial')
        pending.unlink(missing_ok=True)
        try:
            if direct_copy:
                shutil.copy2(source, pending)
            else:
                link_or_copy(source, pending)
            os.replace(pending, target)
        finally:
            pending.unlink(missing_ok=True)

    for name, source in payload.items():
        publish(name, source, name in generated_files)
    for name, source in metadata.items():
        publish(name, source, True)

    # Remove obsolete generated assets, not saves or unknown player files.
    owned_dirs = set(OWNED) | {'drivers', 'host', 'overlay'}
    owned_root = {'shaders.dmsv', 'nintendo_ext.ttf', 'standard.ttf',
                  'fpslock.tsv', 'icon.png', 'languages.txt', 'drivers.txt',
                  'settings.txt', 'env.txt'}
    for root, dirs, files in os.walk(dst, topdown=False, followlinks=False):
        for filename in files:
            path = Path(root) / filename
            rel = path.relative_to(dst).as_posix()
            if rel in desired or rel == previous.name or rel.startswith('saves/'):
                continue
            if rel.split('/', 1)[0] in owned_dirs or rel in owned_root:
                path.unlink()
        for dirname in dirs:
            folder = Path(root) / dirname
            if folder.is_symlink():
                continue
            if folder.relative_to(dst).parts[0] in owned_dirs:
                try:
                    folder.rmdir()
                except OSError:
                    pass

    # Only a fully published pack gets a valid manifest.
    os.replace(staging / MANIFEST, current)
    previous.unlink(missing_ok=True)
    return True


def build(dst, *, profile, version, display, patched, romfs, shaders, aoc=None, sysdata=None, language='en-US', package=None, env=(), settings=None, files=None, text=None,
          host=None, icon=None, drivers=None, profile_h=None, pack_base=None, romfs_at=None, progress=None, require_links=False):
    """Write the pack to dst (replacing it). profile: the AndroidProfile; package: the app's, when not the profile's; env: more env.txt rows, after the profile's; settings: settings.txt values over the profile's;
    files: {name in the pack: source} (the 60 fps memory writes, fpslock.tsv; the button-icon font; subfolders for the app's mode: host/, drivers/, overlay/, icon.png);
    text: {name in the pack: its text} (the launcher's lists: languages.txt, drivers.txt); host/icon/drivers: the app's artifacts, named in the manifest;
    pack_base: where the pack lives on the device (the app's mode carries it in its own folder; else the app's files folder); profile_h: sha256 of the game_profile.h the host was built with (porter host);
    romfs_at: a folder on the device that holds the game's RomFS (the app's mode: the build folder's own RomFS). The pack does not carry the RomFS then: its env.txt says where the host reads it from,
    and the manifest says the same, so the app knows the pack needs that folder.
    Returns the manifest"""
    package = package or profile.package
    if romfs_at: romfs = None   # the RomFS is read from that folder, not from the pack
    tmp = dst.with_name(dst.name + '.partial')
    shutil.rmtree(tmp, ignore_errors=True)   # the half-made pack of a killed port: it may be gone already, and one that vanishes while it is removed must not stop the port
    tmp.mkdir(parents=True)
    # List the file-backed inputs before staging. If a prior standalone
    # companion still matches, keep its directory and its manifest hash.
    payload = {}
    for what, src, sub in (('the rewritten modules', patched, 'mods'), ('the RomFS', romfs, 'RomFS'), ('the DLC', aoc, 'aoc'), ('the system data', sysdata, 'sysdata')):
        if not src: continue
        if progress: progress(what)
        for root, _, names in os.walk(src):
            for name in names:
                path = Path(root) / name
                payload[(Path(sub) / path.relative_to(src)).as_posix()] = path
    if shaders: payload['shaders.dmsv'] = shaders
    for name, src in (files or {}).items():
        payload[name] = Path(src)
    n = len(payload)
    b = sum(path.stat().st_size for path in payload.values())
    for name, body in (text or {}).items(): (tmp / name).write_text(body, encoding='utf-8', newline='\n'); n += 1; b += len(body.encode('utf-8'))
    base = pack_base or str(dst)   # where the pack lives on the device: its own folder (the app's mode); the PC tool's install puts it in the app's files folder
    rows = [f'# Written by nx2android\'s porter for {profile.name} ({profile.id}); KEY=VALUE per line, read by the app at launch',
            f'DM_LANG={language}']
    if romfs_at: rows.append(f'DM_ROMFS={romfs_at}')   # the game's RomFS is read from there, not from the pack (the app's mode: the build folder's own)
    if aoc: rows.append(f'DM_AOC_DIR={base}/aoc')   # the pack's own folder
    if sysdata: rows.append(f'DM_SYSDATA={base}/sysdata')
    rows += profile.env + list(env)
    (tmp / 'env.txt').write_text('\n'.join(rows) + '\n', encoding='utf-8', newline='\n')
    if sets := {**profile.settings, **(settings or {})}: (tmp / 'settings.txt').write_text(''.join(f'{k}={v}\n' for k, v in sets.items()), encoding='utf-8', newline='\n')
    man = dict(title_id=profile.id, name=profile.name, package=package, version=version, display=display, language=language, requires=profile.requires,
               status=profile.status,   # the profile's own state (playable / wip / boots / experimental): the app tells the player what to expect
               graphics=profile.graphics, lan=profile.lan,   # what the game needs: its graphics API, and whether it plays over the local network
               files=n, bytes=b, dlc=sorted(int(p.name) for p in aoc.iterdir() if p.is_dir() and p.name.isdigit()) if aoc else [],
               sysdata=sorted(p.name for p in sysdata.iterdir() if p.is_dir()) if sysdata else [], made=time.strftime('%Y-%m-%d %H:%M:%S'))
    if host: man['host'] = host         # the app's mode: the game's host (libdmplay.so) in the pack, the app copies it into its private folder to run the game
    if icon: man['icon'] = icon
    if drivers: man['drivers'] = list(drivers)
    if profile_h: man['profile_h'] = profile_h
    if romfs_at: man['romfs_at'] = romfs_at   # the folder the RomFS is read from: the pack does not carry it, so the app checks that folder before the game runs
    man['player_api_revision'] = PLAYER_API_REVISION   # the runtime interface the pack was built against: the app rebuilds the runtime when its own revision differs
    if pack_base == '.' and not require_links and dst.is_dir() and progress:
        progress('checking the previous companion data')
    if pack_base == '.' and not require_links and reusable_companion(dst, man, tmp, payload):
        shutil.rmtree(tmp)
        if progress: progress('reused the previous companion data')
        return read_manifest(dst)
    for name, src in payload.items():
        if name in (files or {}):
            dest = tmp / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dest)
        else:
            link_or_copy(src, tmp / name, require_links=require_links)
    (tmp / MANIFEST).write_text(json.dumps(man, indent=2), encoding='utf-8')
    if not require_links and refresh_in_place(dst, tmp, payload, set(files or {}), man, progress):
        shutil.rmtree(tmp)
        return man
    # the exported saves (the player's backups) are not the build's own: the pack is rebuilt whole, so they are put aside and put back
    keep = dst.with_name(dst.name + '.saves-keep')
    shutil.rmtree(keep, ignore_errors=True)
    exported = dst / 'saves'
    if exported.is_dir(): shutil.move(str(exported), str(keep))
    shutil.rmtree(dst, ignore_errors=True)   # the pack of the last port: it may be gone already (the player deleted it), and the files it holds are the build's own (hard links)
    for i in range(20):   # Windows: a folder just removed can stay delete-pending while another process (an indexer, a virus scan) holds it, and the rename is refused meanwhile
        try: tmp.rename(dst); break
        except PermissionError:
            if i == 19: raise
            time.sleep(0.5)
    if keep.is_dir():
        shutil.move(str(keep), str(dst / 'saves'))   # the backups are back where the save manager looks for them
    shutil.rmtree(keep, ignore_errors=True)
    return man


def read_manifest(d):
    try: return json.loads((Path(d) / MANIFEST).read_text(encoding='utf-8'))
    except (OSError, ValueError): return None
