"""Create strict end-user Windows payloads, a portable ZIP and optional setup EXE.

Development tool only. Diagnostics, build notes and contribution documents never
enter an end-user payload. Corresponding third-party source archives are retained
as licensed component material, not as application documentation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def manifest(folder):
    files = [dict(path=p.relative_to(folder).as_posix(), bytes=p.stat().st_size,
                  sha256=hashlib.sha256(p.read_bytes()).hexdigest())
             for p in sorted(folder.rglob('*')) if p.is_file() and p.name != 'manifest-native.json']
    (folder/'manifest-native.json').write_text(json.dumps(dict(schemaVersion=1,
        runtime='C++17 / JUCE', pythonRuntime=False, files=files), indent=2), encoding='utf8')
    return files

def prepare(source, output, version):
    if output.exists() and any(output.iterdir()):
        raise SystemExit('Choose an empty output directory; existing releases are never overwritten.')
    output.mkdir(parents=True, exist_ok=True)
    installed = output/'.staging/installed'
    portable = output/'.staging/portable/Kithara Nea'
    for p in source.rglob('*'):
        if not p.is_file():
            continue
        rel = p.relative_to(source)
        name = rel.as_posix()
        if len(rel.parts) == 1:
            keep = p.suffix.lower() in ('.exe', '.dll') or p.name in (
                'LICENSE_KITHARA_NEA.md', 'LICENSE_KITHARA_NEA_EN.md')
        else:
            keep = rel.parts[0] == 'resources' and rel.parts[1] != 'native-tests'
            if name in ('resources/notices/ffmpeg/BUILD.md',
                        'resources/notices/ffmpeg/build_ffmpeg_audio.py.txt'):
                keep = False
        if not keep or name == 'manifest-native.json':
            continue
        if p.suffix.lower() in ('.py', '.pyc', '.pyd', '.ckpt', '.pdb'):
            raise ValueError('Unexpected runtime file: '+name)
        if p.suffix.lower() == '.md' and not any(key in name.lower() for key in ('license', 'notice', 'legal/')):
            raise ValueError('Unexpected document: '+name)
        target = installed/rel
        # README files supplied by sound-bank authors contain attribution and
        # licensing terms. Keep their contents, with an explicit notice name.
        if p.name.lower() == 'readme.txt' and '/notices/' in name:
            target = target.with_name('ATTRIBUTION.txt')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, target)
    legal = installed/'resources/notices/ffmpeg'
    upstream = source/'resources/notices/ffmpeg/source/ffmpeg-n8.1.zip'
    if upstream.exists():
        with zipfile.ZipFile(legal/'corresponding-source.zip', 'w', zipfile.ZIP_DEFLATED) as z:
            z.write(upstream, 'ffmpeg-n8.1.zip')
            z.write(ROOT/'tools/build_ffmpeg_audio.py', 'build_ffmpeg_audio.py')
            z.write(ROOT/'documentation/BUILD-FFMPEG-AUDIO.md', 'BUILD.md')
            z.write(source/'resources/notices/ffmpeg/provenance.json', 'provenance.json')
    elif not (legal/'corresponding-source.zip').is_file():
        raise ValueError('Corresponding FFmpeg sources are missing')
    # The source is now together with its build recipe in the component archive.
    archived = legal/'source/ffmpeg-n8.1.zip'
    if archived.exists():
        archived.unlink()
        archived.parent.rmdir()
    notice = installed/'resources/notices/FFmpeg-license-and-build.txt'
    text = notice.read_text(encoding='utf8')
    text = text[:text.index('Corresponding sources,')] + (
        'Corresponding sources, exact build options and build recipe: '
        'ffmpeg/corresponding-source.zip. Components retain their own licenses.\n')
    notice.write_text(text, encoding='utf8')
    notice = installed/'resources/whisper/NOTICE.txt'
    text = notice.read_text(encoding='utf8').replace('See LYRIC-TIMING-PILOT.md.', '').replace('See WHISPER-VULKAN.md.', '')
    notice.write_text(text, encoding='utf8')
    # Audio DLL source material is mandatory in both package variants.
    lock = json.loads((ROOT/'resources/notices/audio-source-lock.json').read_text(encoding='utf8'))
    for component in lock['components']:
        dll = installed/'resources/fluidsynth'/component['dll']
        if hashlib.sha256(dll.read_bytes()).hexdigest() != component['binarySha256']:
            raise ValueError('Audio DLL differs from documented upstream binary: '+component['dll'])
        source_archive = installed/'resources/notices'/component['id']/'corresponding-source.zip'
        if not source_archive.is_file():
            raise ValueError('Missing corresponding audio sources; run tools/prepare_audio_sources.py')
        with zipfile.ZipFile(source_archive) as z:
            for item in lock['inputs']:
                if item['name'] in component['sources']:
                    if hashlib.sha256(z.read(item['name'])).hexdigest() != item['sha256']:
                        raise ValueError('Audio source hash mismatch: '+item['name'])
    manifest(installed)
    shutil.copytree(installed, portable)
    (portable/'portable.flag').write_bytes(b'')
    manifest(portable)
    archive = output/f'kithara-nea-{version}-windows-x64-portable.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in sorted(portable.rglob('*')):
            if p.is_file():
                z.write(p, p.relative_to(portable.parent).as_posix())
    return installed, portable, archive

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--input', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--version', default='0.2.2')
    ap.add_argument('--iscc', type=Path)
    ap.add_argument('--compile-existing', action='store_true', help='Compile an already prepared payload')
    args=ap.parse_args()
    if args.compile_existing:
        installed = args.output.resolve()/'.staging/installed'
        archive = args.output.resolve()/f'kithara-nea-{args.version}-windows-x64-portable.zip'
        if not (installed/'manifest-native.json').is_file() or not archive.is_file():
            raise SystemExit('Prepare and verify the payload first.')
    else:
        installed, portable, archive=prepare(args.input.resolve(), args.output.resolve(), args.version)
    if args.iscc:
        licenses = args.output.resolve()/'.staging/installer-licenses'
        licenses.mkdir(parents=True, exist_ok=True)
        for code, name in [('fr','LICENSE_KITHARA_NEA.md'), ('en','LICENSE_KITHARA_NEA_EN.md')]:
            shutil.copy2(installed/name, licenses/f'license-{code}.txt')
        subprocess.run([str(args.iscc), '/DPayload='+str(installed),
            '/DLicenseRoot='+str(licenses),
            '/DReleaseOutput='+str(args.output.resolve()), str(ROOT/'installer/windows.iss')], check=True)
    assets=[p for p in args.output.iterdir() if p.is_file() and p.suffix in ('.zip','.exe')]
    (args.output/'.staging/SHA256SUMS.txt').write_text(''.join(
        hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in sorted(assets)), encoding='ascii')
    print(archive)

if __name__=='__main__':
    main()
