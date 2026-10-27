# Building KITHÁRA NÉA on Windows

## Environment

- Windows 10 or later, x64.
- Visual Studio 2022 Community or Build Tools with Desktop development with C++ and a Windows 10/11 SDK.
- CMake 3.24 or later on PATH, PowerShell 7 and Python 3.10 or later for development tools. The application has no Python dependency.
- Optional: Inno Setup 6.3 or later for the installer, a Vulkan SDK for rebuilding the GPU lyric decoder, and 7-Zip for rebuilding FFmpeg.

The JUCE, ONNX API headers, libsoxr and JSON sources are under `native/`, with their notices. Third-party licensing and applicable JUCE licensing conditions remain the developer's responsibility.

## Verified runtime resources

Models, sound banks, large source archives and prebuilt libraries are distributed in the release rather than committed to the source repository. The pinned portable archive and SHA-256 are in `resources/runtime-lock.json`.

```powershell
python tools/fetch_runtime.py
```

For offline development, supply the matching portable archive:

```powershell
python tools/fetch_runtime.py --archive C:/Downloads/kithara-nea-0.2.1-windows-x64-portable.zip
```

The script verifies the archive and every component before copying resources. It never runs the downloaded application. Use the pinned version; other releases may have incompatible manifests. Small is included; Medium is not needed for building.

## Compile and run

From PowerShell 7 in the repository root:

```powershell
./tools/build_juce.ps1 -Output dist/development/kithara-nea
python tools/stage_whisper.py dist/development/kithara-nea
./dist/development/kithara-nea/kithara-nea.exe
```

Both native targets are compiled and the harmonic component manifest is refreshed. The pinned CPU/Vulkan lyric binaries are reused. Run from the packaged directory with its complete resources and DLLs.

Direct CMake commands are also supported:

```powershell
cmake -S . -B build-juce -G "Visual Studio 17 2022" -A x64 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CONFIGURATION_TYPES=Release
cmake --build build-juce --config Release --target KitharaNea kithara-lv-native --parallel 4
```

The application is `build-juce/KitharaNea_artefacts/Release/KITHÁRA NÉA.exe`; the packaging script supplies its required DLLs. Compilation publishes nothing.

## Checks

```powershell
Start-Process -Wait -FilePath dist/development/kithara-nea/kithara-nea.exe -ArgumentList '--verify-native', 'C:/absolute/path/checks'
```

Inspect `checks.json` for `passed: true`. Fixtures are in `resources/native-tests`; they are excluded from user releases. Checks cover musical definitions, numeric fixtures, projects, alignment, exports, UI state, CPU availability, GPU fallback and cancellation. They use an isolated profile. Microphone and output quality still require listening tests.

## End-user packages

```powershell
./tools/build_release.ps1 -Output dist/releases/0.2.1
```

To build an installer too:

```powershell
./tools/build_release.ps1 -Output dist/releases/0.2.1-with-setup -Iscc 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe'
```

Use a new, empty output folder. This builds, stages the DTW components and creates a strict portable ZIP. The optional installer offers English/French, presents the corresponding license and seeds the first-run language. Current-user mode needs no elevation; all-users mode requests administrator access and uses Program Files. An unprivileged attempt to use Program Files is rejected with an explanatory message. Existing preferences take priority. `portable.flag` selects adjacent `data` storage instead of the installed user profile.

Only the application, components, licensing material and user-manual PDFs enter releases. Component source archives required for redistribution remain in their licensing directories. Test fixtures, contribution instructions and build notes are excluded. Build-only files/checksum reports stay in `.staging/` and are not release assets.

## Rebuilding components

### Audio library source material

`build_release.ps1` runs `python tools/prepare_audio_sources.py` before staging.
This verifies the FluidSynth/libsndfile DLLs against pinned official archives
and prepares their corresponding-source archives and codec notices. Downloads
are cached under `build/legal-audio`; the same command then works offline.
The source archives include upstream build workflows and dependency port recipes.
The package step rejects missing or mismatched source material. It does not
recompile or replace the DLLs. Archive provenance records the evidence and the
limits of the historical upstream build metadata.

### Whisper

The preparation scripts download pinned official whisper.cpp v1.9.1 sources, verify SHA-256 and apply the complete DTW interval patch. The GPU decoder adds an isolated Vulkan device probe; no driver is installed.

```powershell
python tools/prepare_whisper_dtw.py
cmake -S build/whisper-dtw/whisper.cpp-1.9.1 -B build/whisper-dtw/compiled -A x64 -DBUILD_SHARED_LIBS=OFF -DGGML_BACKEND_DL=OFF -DGGML_NATIVE=OFF -DGGML_AVX2=ON -DWHISPER_BUILD_TESTS=OFF
cmake --build build/whisper-dtw/compiled --config Release --target whisper-cli --parallel 4
python tools/prepare_whisper_vulkan.py --sdk "$env:VULKAN_SDK"
```

`stage_whisper.py` prefers rebuilt binaries and refreshes their manifest. Retain the applicable notices and document modified third-party code when distributing custom builds. The Vulkan SDK is a build dependency only.

### Harmonic and rhythm models

Normal builds reuse pinned ONNX models and analytical CQT tables from the runtime resources. Their provenance and licenses accompany them. `tools/export_beat_onnx.py` is a development conversion utility; running the application needs no PyTorch. Model or preprocessing changes require independent numerical/musical validation and matching manifests.

### FFmpeg and manuals

See [the audio-only FFmpeg build instructions](documentation/BUILD-FFMPEG-AUDIO.md) and its shipped corresponding-source archive. To rebuild user manuals, install `reportlab` and `pypdf` in a development virtual environment and run `python tools/generate_manuals.py`. The generator uses Windows Segoe UI fonts. Only the resulting FR/EN PDFs are packaged.

## Source layout

- `native/app`: application, audio, project format, views, exports and checks.
- `native/common`: shared CPU budget.
- `native/harmony`: CQT, ensemble inference and HMM decoding.
- `native/vendor` and `native/harmony/vendor`: source dependencies.
- `resources`: musical data, translations, notices, fixtures and fetched components.
- `tools`: build, preparation and packaging utilities.
- `installer/windows.iss`: bilingual installer.

No developer-specific absolute path is required. `KITHARA_PROFILE` and `KITHARA_RESOURCES` redirect profiles/resources for isolated tests. Leave them unset for normal installed or portable use.
