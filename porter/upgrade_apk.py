"""Upgrade the existing standalone APK without a guest recompilation or asset extraction."""
import hashlib
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from porter import config, host, resources, runtime_update, shader_provenance, signing, toolchain
from porter.upstream import BuildError
from porter.paths import DM
from porter.revision import PLAYER_API_REVISION


def check_runtime_abi(header):
    # ELF64, little endian, ET_DYN, EM_AARCH64.
    if (len(header) < 20 or header[:7] != b'\x7fELF\x02\x01\x01'
            or struct.unpack_from('<HH', header, 16) != (3, 183)):
        raise BuildError('The standalone runtime must be an arm64-v8a ELF shared library.')


def check_shader_header(header, size):
    if len(header) != 12 or header[:4] != b'DMSV':
        raise BuildError('The existing translated shader bundle has an invalid header.')
    version, count = struct.unpack_from('<II', header, 4)
    if version not in (1, 2) or 12 + 32 * count > size:
        raise BuildError('The existing translated shader bundle is incompatible with this runtime.')


class StoredAsset(io.RawIOBase):
    """A seekable view of a stored APK asset. ZipExtFile.seek rescans from byte zero."""
    def __init__(self, stream, entry):
        self.stream = stream
        stream.seek(entry.header_offset)
        header = stream.read(30)
        if len(header) != 30 or header[:4] != b'PK\x03\x04':
            raise BuildError('Invalid standalone APK asset header.')
        name, extra = struct.unpack_from('<HH', header, 26)
        self.start = entry.header_offset + len(header) + name + extra
        self.size = entry.file_size
        self.offset = 0

    def tell(self):
        return self.offset

    def seekable(self):
        return True

    def readable(self):
        return True

    def seek(self, offset, whence=io.SEEK_SET):
        origin = {io.SEEK_SET: 0, io.SEEK_CUR: self.offset, io.SEEK_END: self.size}[whence]
        if origin + offset < 0:
            raise OSError('seek before asset')
        self.offset = origin + offset
        return self.offset

    def read(self, size=-1):
        left = max(0, self.size - self.offset)
        self.stream.seek(self.start + min(self.size, self.offset))
        result = self.stream.read(left if size < 0 else min(size, left))
        self.offset += len(result)
        return result


COMPATIBILITY_ASSET = 'assets/nx2a-upgrade-v1.json'
MAX_COMPATIBILITY_BYTES = 16 << 20


def companion_compatibility(pack, output):
    """Serialize only compatibility inputs, never game payloads or private signing state."""
    pack = Path(pack)
    files = [pack/'nx2a-data.json', pack/'mods/build_src.sha256']
    files += sorted((pack/'mods').glob('*.cuts.tsv'))
    files += sorted((pack/'mods').glob('*.obs.tsv'))
    if not all(p.is_file() for p in files):
        return False
    if sum(p.stat().st_size for p in files) > MAX_COMPATIBILITY_BYTES // 2:
        return False
    record = {p.relative_to(pack).as_posix(): p.read_text(encoding='utf-8') for p in files}
    # Preserve the manifest's exact bytes (including CRLF) for its descriptor hash.
    record['nx2a-data.json'] = (pack/'nx2a-data.json').read_bytes().decode('utf-8')
    shader = pack/'shaders.dmsv'
    shader_info = None
    if shader.is_file():
        with shader.open('rb') as stream:
            shader_info = [stream.read(12).hex(), shader.stat().st_size]
    raw = json.dumps({'schema': 1, 'files': record, 'shader': shader_info}, ensure_ascii=False).encode('utf-8')
    if len(raw) > MAX_COMPATIBILITY_BYTES:
        return False
    Path(output).write_bytes(raw)
    return True


def read_compatibility(archive, descriptor, read_pack):
    entry = archive.getinfo(COMPATIBILITY_ASSET)
    if not 0 < entry.file_size <= MAX_COMPATIBILITY_BYTES:
        raise BuildError('Invalid APK upgrade compatibility record size.')
    info = json.loads(archive.read(entry))
    if not isinstance(info, dict) or info.get('schema') != 1 or not isinstance(info.get('files'), dict):
        raise BuildError('Invalid APK upgrade compatibility record.')
    files = info['files']
    if not all(isinstance(name, str) and isinstance(value, str) for name, value in files.items()):
        raise BuildError('Invalid APK upgrade compatibility inputs.')
    manifest = files['nx2a-data.json'].encode('utf-8')
    if hashlib.sha256(manifest).hexdigest() != descriptor['manifest_sha256']:
        raise BuildError('APK upgrade compatibility record does not match its companion manifest.')
    names = set(files)
    shader = info.get('shader')
    if shader is not None:
        if not isinstance(shader, list) or len(shader) != 2 or not isinstance(shader[0], str) or type(shader[1]) is not int:
            raise BuildError('Invalid APK shader compatibility record.')
        names.add('shaders.dmsv')
    return read_pack(names, lambda name: files[name].encode('utf-8'),
                     lambda: (bytes.fromhex(shader[0]), shader[1]))


def metadata(apk, data, *, runtime=True):
    shader = 'shaders.dmsv'

    def read_pack(names, read, shader_header):
        manifest = json.loads(read('nx2a-data.json'))
        if not isinstance(manifest, dict):
            raise BuildError('Invalid standalone game manifest.')
        if not runtime:
            return manifest, '', []
        revision = manifest.get('player_api_revision')
        if revision is not None and (type(revision) is not int or not 1 <= revision <= PLAYER_API_REVISION):
            raise BuildError(f'Unsupported original APK runtime interface revision: {revision}.')
        if shader in names:
            header, size = shader_header()
            check_shader_header(header, size)
            if 'shader_product' in manifest:
                try:
                    shader_provenance.validate(manifest['shader_product'], header, size)
                except ValueError as exc:
                    raise BuildError(f'Original shader product compatibility: {exc}.') from exc
        elif 'shader_product' in manifest:
            raise BuildError('Original shader product is missing its translated bundle.')
        record = read('mods/build_src.sha256').decode()
        modules = []
        for name in sorted(names):
            if name.startswith('mods/') and name.endswith('.cuts.tsv'):
                obs = name[:-len('.cuts.tsv')] + '.obs.tsv'
                modules.append(host.module_tables(read(name).decode(), read(obs).decode() if obs in names else ''))
        return manifest, record, modules

    with zipfile.ZipFile(apk) as z:
        try:
            with z.open('lib/arm64-v8a/libdmhost.so') as lib:
                check_runtime_abi(lib.read(20))
            if 'assets/nx2a-data.zip' in z.namelist():
                entry = z.getinfo('assets/nx2a-data.zip')
                if entry.compress_type == zipfile.ZIP_STORED:
                    with Path(apk).open('rb') as source, StoredAsset(source, entry) as stream:
                        return read_embedded(stream, read_pack, shader)
                with z.open(entry) as stream:
                    return read_embedded(stream, read_pack, shader)

            descriptor = json.loads(z.read('assets/nx2a-companion.json'))
            if data is None:
                if COMPATIBILITY_ASSET in z.namelist():
                    return read_compatibility(z, descriptor, read_pack)
                raise BuildError('This older companion APK needs its matching original data folder for compatibility checks.')
            data = Path(data)
            if hashlib.sha256((data/'nx2a-data.json').read_bytes()).hexdigest() != descriptor['manifest_sha256']:
                raise BuildError('Companion data does not match this APK.')
            names = {p.relative_to(data).as_posix() for p in (data/'mods').glob('*.tsv')}
            shader_path = data / shader
            if shader_path.is_file():
                names.add(shader)

            def shader_header():
                with shader_path.open('rb') as src:
                    return src.read(12), shader_path.stat().st_size

            return read_pack(names, lambda name: (data/name).read_bytes(), shader_header)
        except KeyError as exc:
            raise BuildError(f'Original standalone APK or data is missing {exc}.') from exc


def read_embedded(stream, read_pack, shader):
    with zipfile.ZipFile(stream) as pack:
        def shader_header():
            with pack.open(shader) as src:
                return src.read(12), pack.getinfo(shader).file_size

        return read_pack(set(pack.namelist()), pack.read, shader_header)


def upgrade_game(apk, output, *, package, version_floor, patch, base, keys, data=None,
                 key=None, jobs=None, no_lsfg=False, firmware=None, touch=False,
                 fps60=False, mt_replay=False, alias_headroom=None, shared_font=None):
    from porter import dumps
    from porter.pipeline import GameUpdate, Options, Port
    apk, output = Path(apk).resolve(), Path(output).resolve()
    if not output.is_dir() or any(output.iterdir()):
        raise BuildError('Game upgrades require an empty, separate staging directory.')
    manifest, _, _ = metadata(apk, data, runtime=False)
    title, current = str(manifest.get('title_id', '')).upper(), str(manifest.get('version', ''))
    if not host.TITLE_ID.fullmatch(title) or not re.fullmatch(r'v[0-9]+', current):
        raise BuildError('The selected APK is missing its original game title or version.')
    if package != manifest.get('package'):
        raise BuildError('The selected APK package does not match its game manifest.')
    key = signing.original_key(apk, title=title, explicit=key)
    patch, base = Path(patch).resolve(), Path(base).resolve()
    for path in (base, patch):
        if not path.is_file() or path.suffix.lower().lstrip('.') not in dumps.EXTS:
            raise BuildError(f'Unsupported or unavailable game dump: {path.name}')
    if patch == base:
        raise BuildError('Choose separate original base and game update dumps.')
    with zipfile.ZipFile(apk) as archive:
        embedded = 'assets/nx2a-data.zip' in archive.namelist()
        previous = None
        if not embedded:
            descriptor = json.loads(archive.read('assets/nx2a-companion.json'))
            baked = 'assets/nx2a-data.txt'
            old_path = archive.read(baked).decode().strip() if baked in archive.namelist() else ''
            if not old_path or not Path(old_path).is_dir():
                old_path = str(Path(data).resolve()) if data is not None else ''
            previous = (old_path, descriptor['manifest_sha256'])
    result = Port(Options(
        folder=base, out=output, keys=Path(keys) if keys else None, package=package,
        self_contained=embedded, language=manifest.get('language') or '', no_lsfg=no_lsfg,
        firmware=firmware, jobs=resources.safe_jobs(jobs), touch=touch, fps60=fps60,
        mt_replay=mt_replay, alias_headroom=alias_headroom, shared_font=shared_font,
        game_update=GameUpdate(patch, title, current, version_floor, key, previous),
    )).run()
    built = Path(result['apk'])
    with zipfile.ZipFile(apk) as source, zipfile.ZipFile(built) as candidate:
        from apkdriver import source_certificate
        if source_certificate(apk, source.start_dir) != source_certificate(built, candidate.start_dir):
            raise BuildError('The completed APK has a different signing certificate.')
    return built


def activity_dex(apk, scratch):
    from porter.pipeline import launcher_dex
    with zipfile.ZipFile(apk) as z:
        manifest = z.read('AndroidManifest.xml')
    launcher = 'com.nx2a.launcher.LauncherActivity'.encode('utf-16le') in manifest
    javac = shutil.which('javac')
    roots = toolchain._sdk_roots()
    platforms = [p for root in roots for p in (root/'platforms').glob('android-*/android.jar')]
    d8s = [p for root in roots for p in (root/'build-tools').glob('*/d8' + ('.bat' if os.name == 'nt' else ''))]
    def version(p): return tuple(int(x) for x in re.findall(r'\d+', p.parent.name))
    if javac and platforms and d8s:
        platform, d8 = max(platforms, key=version), max(d8s, key=version)
        classes, dex = scratch/'classes', scratch/'dex'
        classes.mkdir(); dex.mkdir()
        sources = sorted((DM/'android/java/org/nx2a').glob('*.java'))
        subprocess.run([javac, '-source', '8', '-target', '8', '-classpath', str(platform), '-d', str(classes), *map(str,sources)], check=True)
        inputs = sorted(classes.rglob('*.class'))
        if launcher:
            extra = launcher_dex()
            if not extra: raise BuildError('The original APK requires the optional launcher DEX; prepare the matching launcher first.')
            inputs.append(extra)
        subprocess.run([str(d8), '--min-api', '29', '--lib', str(platform), '--output', str(dex), *map(str,inputs)], check=True)
        out = dex/'classes.dex'
    else:
        out = DM.parent.parent/'tools/game-activity.dex'
        if not out.is_file(): raise BuildError('Updated bundled game-activity.dex or Android SDK/JDK required.')
    if launcher and b'Lcom/nx2a/launcher/LauncherActivity;' not in out.read_bytes():
        raise BuildError('The updated DEX lacks the APK launcher; refusing an incompatible update.')
    return out


def upgrade(apk, output, *, data=None, key=None, jobs=None, no_lsfg=False, debug=False):
    apk, output = Path(apk).resolve(), Path(output).resolve()
    if apk == output: raise BuildError('Choose a separate output APK.')
    manifest, record, modules = metadata(apk, data)
    if manifest.get('shader_product'):
        product = manifest['shader_product']
        print(f"Reusing shader product {product['sha256']}; input key {product['input_key']}; "
              f"translation {'complete' if product['complete'] else 'partial'} "
              "(recorded identity; header/size checked, content not rehashed).", flush=True)
    elif manifest.get('graphics') != 'OpenGL ES':
        print("Legacy shader product provenance unavailable; existing structural compatibility checks apply.", flush=True)
    title = manifest.get('title_id', '').upper()
    fingerprint = manifest.get('profile_h')
    if not host.TITLE_ID.fullmatch(title) or not fingerprint:
        raise BuildError('Original game profile identity is missing.')
    key = signing.original_key(apk, title=title, explicit=key)
    profile, _, _ = runtime_update.find_profile(title, fingerprint)
    if profile is None: raise BuildError('The original profile is unavailable; refusing a potentially incompatible runtime.')
    if not record.strip(): raise BuildError('Original guest rewriter fingerprint is missing.')
    stale = host.stale_sources(record, title, modules=modules)
    if stale: raise BuildError('Guest rewrite inputs changed: ' + ', '.join(stale))
    ndk, _ = toolchain.find_ndk(config.load())
    if ndk is None: raise BuildError('Android NDK unavailable; prepare the compiler environment first.')
    root = Path(os.environ.get('NX2A_WORK') or tempfile.gettempdir())/'runtime'
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='apk-upgrade-', dir=root) as tmp:
        scratch = Path(tmp)
        # The header includes the profile source path. Use the same profile
        # location for both compatibility checks and the compiler invocation.
        env = dict(os.environ, DM_GAME=title, DM_GAMES_DIR=str(profile.parent), DM_GAMES_FIRST='1')
        tc_lib = DM.parent.parent/'toolchain/lib'
        if tc_lib.is_dir(): env['LD_LIBRARY_PATH'] = str(tc_lib) + os.pathsep + env.get('LD_LIBRARY_PATH', '')
        dex = activity_dex(apk, scratch)
        built = scratch/'host'
        args = [sys.executable, str(DM/'android/build.py'), '--game', title, '--ndk', str(ndk),
                '--out', str(built), '--lib', '--jobs', str(resources.safe_jobs(jobs))]
        if not debug: args.append('--release')
        if no_lsfg: args.append('--no-lsfg')
        subprocess.run(args, env=env, check=True)
        header = built/'gen'/title/'game_profile.h'
        if hashlib.sha256(header.read_bytes()).hexdigest() != fingerprint:
            raise BuildError('Built runtime profile does not match the original APK.')
        library = built/'libdmhost.so'
        with library.open('rb') as lib:
            check_runtime_abi(lib.read(20))
        from porter.pipeline import ui_font_assets, runtime_shader_license_assets
        runtime_assets = {name: path for name, path in ui_font_assets().items() if path.is_file()}
        runtime_assets.update(runtime_shader_license_assets())
        # Legacy companion exports with an available verified folder acquire
        # the compatibility record on their next successful runtime update.
        if data is not None:
            with zipfile.ZipFile(apk) as archive:
                if 'assets/nx2a-data.zip' not in archive.namelist():
                    compatibility = scratch/'nx2a-upgrade-v1.json'
                    if companion_compatibility(data, compatibility):
                        runtime_assets['nx2a-upgrade-v1.json'] = compatibility
        assets = [arg for name, path in runtime_assets.items() for arg in ('--asset', f'{name}={path}')]
        subprocess.run([sys.executable, str(DM/'android/apk/apkdriver.py'), str(apk), str(output),
                        str(key), '--host', str(library), '--dex', str(dex), *assets], check=True)
    return output
