"""Stage the pinned CPU/Vulkan DTW components in a development package."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('package', type=Path)
args = parser.parse_args()
folder = args.package/'resources/whisper'
for backend, name in [('whisper-dtw','whisper-path.exe'),('whisper-vulkan','whisper-vulkan.exe')]:
    source = ROOT/'build'/backend/'compiled/bin/Release/whisper-cli.exe'
    if not source.exists():
        source = ROOT/'resources/whisper/bin'/name
    if not source.exists():
        raise SystemExit('Build or fetch the pinned Whisper component: '+name)
    shutil.copy2(source, folder/'bin'/name)
manifest = json.loads((folder/'manifest-sha256.json').read_text(encoding='utf8'))
for name in ['whisper-path.exe','whisper-vulkan.exe']:
    manifest['bin/'+name] = hashlib.sha256((folder/'bin'/name).read_bytes()).hexdigest()
(folder/'manifest-sha256.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')
(folder/'experimental-timing.json').write_text(json.dumps(dict(dtw=True, displayAnticipationMs=60)), encoding='utf8')
(folder/'NOTICE.txt').write_text('''Whisper models — OpenAI, MIT.
whisper.cpp v1.9.1 — Georgi Gerganov and contributors, MIT.
Sources: https://github.com/openai/whisper ; https://github.com/ggml-org/whisper.cpp
Converted models: https://huggingface.co/ggerganov/whisper.cpp
Original licenses are included in licenses/.
Modified CPU and Vulkan decoders export complete DTW token interval boundaries.
Diagnostic source paths are reduced to file names; decoding operations are unchanged.
The CPU DTW decoder targets x64 AVX2. Vulkan backends are optional and isolated;
the CPU decoder remains usable without Vulkan. No graphics driver or SDK is bundled.
SHA-256 checksums are recorded in manifest-sha256.json.
''', encoding='utf8')
