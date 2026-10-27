# KITHÁRA NÉA

A Windows application for studying chords, following lyrics and preparing song sheets. Audio analysis runs locally; no account or cloud service is required.
https://youtu.be/63XpK9sEHMg
## Download

Get the Windows x64 installer or portable ZIP from [Releases](https://github.com/Trucabulles/Kithara-Nea/releases). Windows 10 or later and an AVX2-capable x64 CPU are required for the bundled lyric decoder. A Vulkan-compatible GPU is optional; CPU processing remains available.

- **Installer:** choose English or French and review the project license in that language. Install for the current user without administrator privileges, or choose all users and authorize administrator access to Program Files. Existing application-language preferences are preserved.
- **Portable:** extract the complete ZIP to a writable folder and open `kithara-nea.exe`. Keep `portable.flag` beside it. Preferences, recordings and optional model downloads stay in the adjacent `data` folder. The first-run language is the application's default, French; English is available from the menu.

Small is bundled for lyric transcription. Thorough quality can download Medium, or use an existing compatible model after integrity verification. Subsequent processing works offline.

## Features

- Chord recognition, tempo and time-signature estimates, with editable results.
- Playback, looping, system input/output selection and recording with countdown, input gain and a live level display.
- Scrolling waveform, five chord cards around the current chord and synchronized lyrics with current-word highlighting.
- Instrument diagrams, calculated fingerings, tuning, capo, transposition and independent piano/string-instrument audition.
- Separate lyric transcription and reference-text alignment, optional Vulkan acceleration and automatic CPU fallback.
- Song sheet editor with chords above words, preserved line breaks and instrumental passages in chronological order.
- Song sheet PDF/ChordPro, measure-based chord chart PDF, harmonic MIDI, accompaniment WAV, CSV and text exports.
- Self-contained or linked `.knea` projects preserving edits and timestamps without rerunning analysis on opening. Application recordings are embedded even in linked projects.
- French and English interface, help and user manuals.

Results are proposals to check by listening. Chord extensions, inversions, sung-word timing and rhythm can require correction. Exported accompaniment realizes recognized harmony; it does not reproduce the original performance note for note.

## Development

The application is C++17 with JUCE. LV-Chordia/ONNX performs harmonic analysis, Beat This small0 supplies rhythmic activations, whisper.cpp transcribes lyrics, FluidSynth renders sound and an audio-focused FFmpeg executable handles additional audio formats. Python is used by development tools only, never by the installed application.

See [BUILDING.md](BUILDING.md) for the environment, verified runtime resources, compilation, checks and packaging. [FORMAT-KNEA.md](FORMAT-KNEA.md) describes the project container. User manual sources are in `documentation/`.

CPU analysis uses half of the logical processors available to the process, rounded down, with a minimum of one and maximum of twelve threads. LV-Chordia, Beat This and Whisper share this policy. Windows affinity and explicit CPU-set restrictions are respected; analysis jobs are sequential.

## License and contributions

Original code and documentation are copyright © 2026 Trucabulles under the **KITHÁRA NÉA License for inspection, modification and non-commercial redistribution**. This is a source-available project, not an assertion of an OSI-approved license. Read the [license in English](LICENSE), or the [original French version](LICENSE_KITHARA_NEA.md). The French original remains authoritative.

Third-party software, models, musical data and sound banks retain their own licenses and notices, included with the components. The application license does not limit commercial use of works created with it, subject to third-party rights.

See [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes. Report reproducible bugs and relevant tests; do not include private recordings, personal projects or resources you lack permission to redistribute.
