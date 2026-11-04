# KITHÁRA NÉA

A Windows application for studying chords, following lyrics and preparing song sheets. Audio analysis runs locally; no account or cloud service is required.

https://youtu.be/63XpK9sEHMg

**Kithára Néa provides a chord transcription that you can check by ear and edit.**

Neural chord recognition generally works better on simple chords. It becomes less reliable when two chords differ by a single note, as with a seventh chord and its corresponding ninth. Depending on the performance and arrangement, that note may be played softly, masked by another instrument, or confused with a passing note.

Overtones and lingering resonance from earlier notes can also be mistaken for notes in the current chord. As a result, the transcription may miss an extension, add one that was not played, or place a chord change too early or too late.

These limitations were common to every chord recognition system we tested. When we checked the transcriptions against the recordings, we found both spurious chord changes and missed extensions, including sixths and sevenths. The errors varied by song and by system.

Kithára Néa ships with fine-tuned weights derived from the original checkpoint published in the MIT-licensed [BTC-ISMIR19 repository](https://github.com/jayg996/BTC-ISMIR19). In our tests, the fine-tuned weights improved chord recognition on some recordings compared with the original weights. Kithára Néa also includes the LV-Chordia weights as an alternative. In our tests, LV-Chordia was most reliable on simpler chords and less consistent on extensions.

Both recognition engines are available in the analysis settings. You can choose BTC 230 or LV-Chordia for each analysis. BTC 230 is selected by default.

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

## Accuracy and limitations

Automatic chord recognition is an interpretation of the recording, not an infallible transcription. Musical ambiguity and recognition errors affect many chord-recognition systems. All models examined in our comparisons showed these recurring categories of limitations, although they handled individual passages differently:

- **Melody and passing tones:** temporary notes can be mistaken for underlying harmony, introducing unnecessary extensions or chord changes.
- **Extended chords:** genuine sixths, sevenths, ninths and other extensions can be omitted, simplified or misclassified, even when their characteristic notes are audible.
- **Bass and inversions:** voicing, instrumentation, playing technique and spectral interference can lead to an incorrect bass or inversion label.
- **Arpeggios and incomplete voicings:** notes played successively, muted or omitted can make the intended harmony difficult to determine.
- **Complex arrangements:** overlapping instruments, harmonics and melodic lines can support several plausible interpretations.

These limitations are shared across the models we have examined; they are not unique to Kithára Néa. Our comparisons are not a comprehensive benchmark of all available models. Missed extensions and inversions are not necessarily deliberate simplifications and remain areas for improvement.

Accuracy depends on the recording, musical style, instrumentation, chord vocabulary and evaluation method. Dataset annotations may describe prescribed chords, performed notes or one harmonic interpretation; these do not always coincide. Agreement with a reference dataset must therefore not be confused with absolute musical accuracy.

Kithára Néa does not claim perfect recognition or universal superiority over other systems. Results remain editable so musicians can review, correct and refine the proposed harmony against the recording.

## Development

The application is C++17 with JUCE. BTC 230 performs harmonic analysis by default, with LV-Chordia available as an alternative, Beat This small0 supplies rhythmic activations, whisper.cpp transcribes lyrics, FluidSynth renders sound and an audio-focused FFmpeg executable handles additional audio formats. Python is used by development tools only, never by the installed application.

See [BUILDING.md](BUILDING.md) for the environment, verified runtime resources, compilation, checks and packaging. [FORMAT-KNEA.md](FORMAT-KNEA.md) describes the project container. User manual sources are in `documentation/`.

CPU analysis uses half of the logical processors available to the process, rounded down, with a minimum of one and maximum of twelve threads. LV-Chordia, Beat This and Whisper share this policy. Windows affinity and explicit CPU-set restrictions are respected; analysis jobs are sequential.

## License and contributions

Original code and documentation are copyright © 2026 Trucabulles under the **KITHÁRA NÉA License for inspection, modification and non-commercial redistribution**. This is a source-available project, not an assertion of an OSI-approved license. Read the [license in English](LICENSE), or the [original French version](LICENSE_KITHARA_NEA.md). The French original remains authoritative.

Third-party software, models, musical data and sound banks retain their own licenses and notices, included with the components. The application license does not limit commercial use of works created with it, subject to third-party rights.

See [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes. Report reproducible bugs and relevant tests; do not include private recordings, personal projects or resources you lack permission to redistribute.
