"""Export a source-only publication snapshot without development history."""
import argparse
from pathlib import Path
import shutil
import subprocess
import json

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('destination', type=Path)
parser.add_argument('--replace-readme', action='store_true', help='Replace the remote placeholder README only')
parser.add_argument('--refresh', action='store_true', help='Refresh the generated publication snapshot')
parser.add_argument('--manifest', type=Path, help='Write the publication file list outside the snapshot')
args = parser.parse_args()
destination = args.destination.resolve()
if destination == ROOT or ROOT.is_relative_to(destination):
    raise SystemExit('Use a separate publication folder.')
destination.mkdir(parents=True, exist_ok=True)
tools = {'build_juce.ps1','build_release.ps1','package_release.py','fetch_runtime.py',
         'stage_whisper.py','prepare_whisper_dtw.py','prepare_whisper_vulkan.py',
         'build_ffmpeg_audio.py','export_beat_onnx.py','generate_manuals.py',
         'export_public_sources.py','prepare_audio_sources.py'}
documents = {'MANUAL-EN.md','MANUEL-FR.md','BUILD-FFMPEG-AUDIO.md'}
roots = {'CMakeLists.txt','.gitignore','.gitattributes','README.md','CONTRIBUTING.md',
         'BUILDING.md','LICENSE','LICENSE_KITHARA_NEA.md','LICENSE_KITHARA_NEA_EN.md','FORMAT-KNEA.md'}
tracked = subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode('utf8').split('\0')
# Include newly authored publication files before they are committed locally.
tracked += [p.relative_to(ROOT).as_posix() for p in (ROOT/'tools').glob('*') if p.is_file()]
tracked += ['BUILDING.md','.gitattributes','installer/windows.iss','resources/runtime-lock.json']
for vendor in ('native/vendor', 'native/harmony/vendor'):
    tracked += [p.relative_to(ROOT).as_posix() for p in (ROOT/vendor).rglob('*')
                if p.is_file() and '.git' not in p.relative_to(ROOT/vendor).parts]
copied=[]
for name in sorted(set(tracked)):
    if not name or name == '.github/FUNDING.yml':
        continue
    p=Path(name);source=ROOT/p
    if not source.is_file() or p.name in ('AGENTS.md','SKILL.md'):
        continue
    keep=name in roots or name.startswith(('native/app/','native/common/','native/harmony/','native/vendor/'))
    keep |= p.parts[0]=='tools' and p.name in tools
    keep |= p.parts[0]=='documentation' and p.name in documents
    keep |= p.parts[0]=='installer'
    keep |= name.startswith(('resources/locales/','resources/native-tests/','resources/legal/',
                            'resources/independent/theory/','resources/independent/brand/'))
    keep |= name=='resources/runtime-lock.json'
    if name.startswith('resources/') and any(x in name.lower() for x in ('license','copying','notice','attribution','provenance')):
        keep |= source.suffix.lower() in ('.txt','.md','.json') or source.suffix==''
    if name.startswith('resources/notices/'):
        keep = name in ('resources/notices/JUCE-LICENSE.md', 'resources/notices/FFmpeg-license-and-build.txt', 'resources/notices/audio-source-lock.json') or name.startswith((
            'resources/notices/ffmpeg/', 'resources/notices/fluidsynth/', 'resources/notices/libsndfile/'))
    if name.startswith('native/vendor/JUCE/examples/'):
        keep = False
    if name.startswith('native/vendor/JUCE/extras/') and not name.startswith('native/vendor/JUCE/extras/Build/'):
        keep = False
    if source.suffix.lower() in ('.dll','.exe','.pdb','.obj','.lib','.sf2','.onnx','.ckpt','.bin','.zip','.xz'):
        keep=False
    if not keep:
        continue
    target=destination/p;target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists() and target.read_bytes()!=source.read_bytes() and not args.refresh and not (args.replace_readme and name=='README.md'):
        raise SystemExit('Publication file already differs: '+name)
    shutil.copy2(source,target);copied.append(name)
if args.manifest:
    args.manifest.parent.mkdir(parents=True,exist_ok=True)
    args.manifest.write_text(json.dumps(copied,indent=2),encoding='utf8')
print('Exported',len(copied),'source, legal and documentation files.')
