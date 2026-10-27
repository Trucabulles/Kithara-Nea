"""Fetch the pinned release components needed for a native development build.

An existing portable archive may be supplied for offline development. The archive
is verified before extraction; no installer or downloaded executable is run.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    lock = json.loads((ROOT/'resources/runtime-lock.json').read_text(encoding='utf8'))
    archive = args.archive or ROOT/'build/runtime'/lock['filename']
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        part = archive.with_suffix(archive.suffix+'.part')
        try:
            urllib.request.urlretrieve(lock['url'], part)
            if hashlib.sha256(part.read_bytes()).hexdigest() != lock['sha256']:
                raise ValueError('Runtime archive SHA-256 mismatch')
            part.replace(archive)
        finally:
            if part.exists():
                part.unlink()
    if hashlib.sha256(archive.read_bytes()).hexdigest() != lock['sha256']:
        raise ValueError('Runtime archive SHA-256 mismatch')
    with zipfile.ZipFile(archive) as z, tempfile.TemporaryDirectory(prefix='kithara-runtime-') as temp:
        folder = Path(temp)
        for info in z.infolist():
            target = (folder/info.filename).resolve()
            if not target.is_relative_to(folder.resolve()):
                raise ValueError('Unsafe runtime archive entry')
        z.extractall(folder)
        source = folder/'Kithara Nea'
        manifest = json.loads((source/'manifest-native.json').read_text(encoding='utf8'))
        for entry in manifest['files']:
            p = source/entry['path']
            if not p.resolve().is_relative_to(source.resolve()) or not p.is_file():
                raise ValueError('Unsafe runtime manifest')
            if p.stat().st_size != entry['bytes'] or hashlib.sha256(p.read_bytes()).hexdigest() != entry['sha256']:
                raise ValueError('Invalid runtime component: '+entry['path'])
        shutil.copytree(source/'resources', ROOT/'resources', dirs_exist_ok=True)
    print('Verified native runtime resources are ready.')

if __name__ == '__main__':
    main()
