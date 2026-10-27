# Audio-focused FFmpeg build

The Windows distribution uses FFmpeg 8.1 as a separate command-line executable.
It decodes local audio files and provides the existing audio resampling,
pitch/rate and tempo filters. It is not linked into the application executable.

## Build environment

On Windows x64, install Python 3.10 or later and 7-Zip, then run:

```powershell
python tools/build_ffmpeg_audio.py
```

The script downloads pinned FFmpeg sources, w64devkit and NASM from their upstream
sources and verifies SHA-256 checksums before extraction. Tools remain under
`build/ffmpeg-audio`; the system PATH is not modified. Windows short paths must
be available when the workspace path contains spaces.

The resulting executable is `build/ffmpeg-audio/compiled/ffmpeg.exe`.
The script stages this executable; it does not install it into the application.
Configuration, compiler logs and provenance are saved beside the build directory.
The normal upstream configure probes are retained. No antivirus exclusion or
protection change is required by the script.

## Enabled functions

- Native audio decoders for AAC, ALAC, MP3, MP2, FLAC, Vorbis, Opus, WMA,
  APE, WavPack, TTA, PCM and the selected LGPL-compatible ADPCM variants.
- Audio containers including WAV, AIFF, MPEG audio, MP4/M4A, Ogg, ASF,
  Matroska and the corresponding lossless-audio containers.
- PCM output, WAV output, local-file and pipe protocols.
- Audio filters needed for conversion, mixing, rate and tempo changes.
- Native x86 optimizations with NASM; no CPU requirement specific to the build machine.

There are no video encoders or decoders, network protocols, external codec
libraries, GPU libraries, FFplay or FFprobe. The upstream FFmpeg CLI selects
a few small built-in video filter helpers even in this restricted configuration;
their presence does not enable video decoding or encoding.

## License and redistribution

The resulting build reports LGPL version 2.1 or later. GPL, version-3-only,
nonfree and autodetected external components are disabled. The upstream source
archive is unchanged; the selection is made using configure options.

Distribution materials are under `resources/notices/ffmpeg`: the exact source
archive, build provenance and options, upstream license information, LGPL text,
and MinGW runtime notices. The source archive SHA-256 is recorded in provenance.
These components retain their own license independently from the application.

## Validation before replacement

Keep the previous decoder while comparing builds. With NumPy installed, run:

```powershell
python tools/verify_ffmpeg_audio.py --baseline resources/bin/ffmpeg.exe --candidate build/ffmpeg-audio/compiled/ffmpeg.exe
```

Use repeated `--audio path/to/file` arguments for real recordings. The comparison
generates independent fixtures covering PCM WAV, AIFF, MP3, AAC/M4A, ALAC,
FLAC, Vorbis, Opus and WMA. It checks sample count and numerical differences,
including mono/16 kHz conversion and the application's rate/tempo filter chain.
Results stay in the ignored `verification/ffmpeg-audio` directory.

Only replace the packaged decoder after comparison and native application checks.
Keep the matching sources and notices with the distribution and update its SHA-256 manifests.
