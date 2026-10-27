# .knea project container — schema 1

A project is a ZIP/ZIP64 container containing `project.json` and, in self-contained mode, the original media bytes under `audio/source.ext`.

`project.json` contains `format: "kithara-nea"`, `schemaVersion: 1`, `document` and `media`. The document preserves chords and time boundaries, theoretical notes, timed lyric lines and words, reference-text line breaks, corrections, selected positions, capo, transposition and tempo. `instrument_family` identifies the visual representation and `audition_timbre` the sound.

`media.mode` is `embedded`, `external` or `none`. Embedded media is checked by size and SHA-256. External media can be relocated when opening. Older JSON documents remain readable; normal saving produces a `.knea` container.

`document.audio_origin: "recording"` identifies a take created in the application. Its audio is always embedded, even in a `_linked.knea` save. This marker survives reopening; only imported files may remain external. The `_linked` suffix therefore does not force `media.mode: "external"` for recordings.

Opening restores saved data without running analysis. Embedded media uses a temporary directory retained while needed by the document or undo history. Writes are atomic and unsupported future schema versions are rejected.

Layout belongs to application preferences: visible tracks, folded panels, selected tab, window size and complementary views. Installed applications store preferences in the user profile; portable builds use their adjacent `data` folder. These settings do not change project timestamps.
