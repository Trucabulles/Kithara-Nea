# KITHÁRA NÉA
## User manual
C++ / JUCE working version • October 2026

KITHÁRA NÉA analyzes chords in a recording, follows lyrics and helps prepare a song sheet. Music analysis and lyric transcription are separate actions, started on demand. Processing takes place locally.

## 1. Open and save your work
The Windows installer offers English or French and displays the corresponding project license. Choose current-user installation for a writable profile folder, or all users to install in Program Files with administrator authorization. Its language is used for the application's first launch; an existing application-language preference takes priority. The portable edition needs no installation: extract the whole ZIP to a writable folder and open kithara-nea.exe. It starts with the application's default language, French, which can be changed from the menu. Keep portable.flag beside the executable. Preferences, recordings and downloaded optional models stay in the adjacent data folder. Move that folder together with the application to retain your work. Temporary processing files use the system temporary folder and are removed when processing ends.

Choose New transcription to open audio. Opening a file does not start analysis. New project in the hamburger menu starts an empty workspace for opening audio or recording. It offers to save unsaved work first. New transcription remains the shortcut for opening audio. The menu also provides project opening, saving and recent projects.

The native .knea project stores chords, lyrics, timestamps, corrections and song settings. A self-contained project embeds the audio; a linked project references the original file. Keep that file available, or locate it again when opening the project. Reopening a project does not run any model.

Save regularly with Ctrl+S. Ctrl+Shift+S saves another version. PDF, ChordPro, MIDI and WAV exports do not replace the working project.

## 2. Analyze and check chords
Analyze chords starts harmonic recognition. Detect beats and tempo also enables rhythm analysis. Replacing an existing chord analysis requires confirmation; Undo restores the preceding results.

The waiting window displays the operation and elapsed time. Esc requests a clean cancellation; the window closes when processing actually stops. Multiple requested jobs run sequentially.

Results are proposals to check by listening. Inversions, extensions and very brief chords may be recognized incorrectly. Tempo and time signature may remain undetermined when evidence is insufficient. These editable rhythm indications are used on the song sheet; they do not move or quantize events.

## 3. Follow playback and anticipate
The waveform retains a time axis. Chord names and short change markers are placed at their actual times. When a project opens or analysis finishes, the zoom is calculated from every change to avoid overlapping names. An extremely brief change can therefore produce a close view.

The mouse wheel and zoom slider then freely adjust the waveform. The slider and Readable zoom button are in the timeline footer. Automatic readable zoom adapts when window width changes. A manual zoom is preserved on resize; Readable zoom re-enables automatic adjustment. Time labels adapt their interval to the visible duration. The waveform scrolls continuously during playback. Click it to seek; middle-drag to pan. Shift-drag defines a timed loop.

The Chords track shows five equal-width positions: two previous events, the current chord in the middle and two following events. This window advances by one event per change, independently of zoom. Unavailable positions at the beginning or end remain empty. Repeated chord occurrences are not merged.

Each card shows the name above the selected fingering. Click a card to select the event and seek to its beginning. Right-click retains editing commands; double-click edits the chord. Fixed cards cannot drag time boundaries: use Edit data instead.

The playhead appears only on the waveform. The current chord and sung word have their own highlights. The two following cards provide anticipation; no additional chord name overlays the waveform or lyrics.

## 4. Playback and sound
The triangle starts playback; pause preserves the current position. The square stops playback and returns to the beginning. The two navigation arrows skip backward or forward five seconds; double-click jumps to the beginning or end. Double-clicking the left track labels also returns to the beginning.

The loop icon enables or disables looping. The red button starts a visual 3–2–1 countdown, then records from the selected input. Space, Esc or a second click cancels the countdown without creating a take. During recording, Space or R finishes and opens the take. Input gain (0 to +24 dB, initially 0 dB) affects only new recordings, independently of playback volume. The captured duration, input level and rolling waveform are displayed; a red trace and warning report clipping. Reduce input gain if clipping occurs. Audio inputs and outputs are available in the hamburger menu. Check the device, channels and microphone effects if no sound is captured. Raw takes are temporarily kept in the application user folder. Saving a project always embeds an application recording, including Save linked project: no separate audio file is required to reopen the saved take.

The speaker below the keyboard plays the displayed piano voicing. The speaker in the bottom-left corner of the fingering panel plays the displayed string position with its tuning and capo. Muted strings do not sound. The sound follows the displayed string family: nylon guitar, ukulele, mandolin or banjo. Bass temporarily uses guitar, explicitly indicated in its tooltip. Individual piano keys always play the piano and highlight while held.

## 5. Instruments, positions and arrangement
The piano is on the left and the detailed string-instrument diagram is on the right. Show chord for selects the represented string instrument. Positions provides its tuning. Click the detailed diagram to cycle through available fingerings; the caption shows their number. Chord cards reflect the chosen position.

Transposition is measured in semitones: +1 raises by one semitone. The detected and transposed keys are shown when available. Capo and tuning affect the generated positions. These settings do not move timestamps or modify the source recording.

Options shows or hides the settings panel. Tracks independently controls waveform, chords and lyrics. Drag the divider grip to adjust the space assigned to the timeline and instruments. The keyboard and diagrams keep their proportions. A smaller window temporarily limits the requested instrument height; enlarging it restores that height when space permits.

The small arrow at the right end of the divider hides or restores the entire instrument area; the resize grip stays in the middle. When hidden, the timeline fills the released space and the zoom controls remain available above the divider. Playback, the selected instrument and chord positions are preserved. Resizing does not reopen a hidden panel. The folded state and requested height are remembered between sessions. The fingering panel shows a single count-and-action line above the proportionally scaled diagram.

## 6. Lyrics and alignment
Transcribe lyrics produces text and time references. Fast uses the Small model included with the application. Choosing Thorough offers a download of Medium (1.43 GiB), with a link to the whisper.cpp source on Hugging Face, progress and Esc cancellation. SHA-256 integrity is checked before the file is installed in your user folder for subsequent offline use. Downloading requires confirmation. Language is detected automatically. Singing, backing vocals and instrumental mixtures may cause uncertain words or timestamps.

Compute defaults to Automatic: a compatible Vulkan GPU is preferred, with CPU used if the GPU is unavailable or fails. CPU and GPU can also be selected explicitly; the choice applies to transcription and alignment and is remembered. A CPU retry after a GPU failure is reported in the status line. The first GPU run may take longer while the graphics driver prepares its cache. The models and 60 ms visual word anticipation are unchanged.

Enter / align lyrics lets you paste your own text. Line breaks and blank lines are preserved. You can keep the text without aligning, then align it to audio later. Review alignment displays the matching time references.

Instrumental audio may yield no words. Without usable matches, entered text and previous lyrics are preserved; unmatched words do not receive invented timestamps.

The Lyrics track shows one stable phrase with generous word spacing and a highlight on the current word. Clicking a timed word seeks to it. In Lyrics-only mode, chord names appear above words, between words, or before/after a phrase according to their times. Instrumental passages still display chords without attaching them to invented syllables.

## 7. Editing and song sheets
Edit data, at the bottom of the settings panel, opens a secondary window with Chords and Timed lyrics tabs. Double-click a cell to edit chord names, bass notes, line text or start/end times in seconds. Enter applies; Escape cancels. The Words column opens an editor for individual word text and timestamps. Correcting one word preserves its times and updates source-text offsets. Replacing a complete line clears only that line's word timestamps; use Words for local corrections that should retain timing. Invalid values and time overlaps are rejected. Undo/Redo and .knea saving include these corrections.

Chord analysis and lyric alignment may run in either order. The automatic song sheet combines the available results without running Whisper again. Existing manually edited sheets are preserved; Recalculate placement explicitly replaces them after confirmation. Older saved sheets are treated as automatic only when they exactly match the previous generator output.

Prepare song sheet is available in the hamburger menu. The Source text tab preserves line breaks. Chords are encoded as [Am] before a word or syllable; the preview displays them above. In the preview, click a word to insert the selected chord. Hover highlights the chord in place. Ctrl+drag a chord to move it with a floating name and drop marker; Escape cancels the gesture. Right-click a chord to delete its sheet annotation immediately, without a menu or confirmation. Ctrl+Z undoes an insertion, deletion or move; Ctrl+Y or Ctrl+Shift+Z redoes it. Insert at cursor remains available in Source text. These edits do not change the musical analysis. Check and correct automatic placement by listening. Diagrams use the project's selected fingerings.

Export offers song sheet PDF or ChordPro, chord chart PDF, MIDI or WAV accompaniment, and chords in CSV or text. MIDI accompaniment realizes the detected harmony; it does not transcribe every note actually played. ChordPro is a reusable text document.

## 8. Language, help and licenses
Choose Français or English in the hamburger menu. The choice is saved; your lyrics and projects are not translated. The ? button opens this manual, keyboard shortcuts, About and credits and third-party licenses.

KITHÁRA NÉA license in About displays Trucabulles's complete license. French is the original text; a courtesy English translation is provided. Third-party components and sound libraries retain their own licenses, available in their original wording in the dedicated window.

## Useful shortcuts
Space: play/pause. R: record/finish. Esc: cancel processing. Ctrl+O: audio. Ctrl+Shift+O: project. Ctrl+S: save. Ctrl+Shift+S: save as. Ctrl+Z / Ctrl+Y: undo/redo. Ctrl+arrows: ±5 seconds. Ctrl+Home / Ctrl+End: beginning/end. Home: readable zoom. Enter: edit chord. Delete: no chord. Ctrl+K: split. Ctrl+M: merge. Ctrl+B: marker.

## Timed Chord Chart PDF
The chord-chart PDF displays three measure cells per row, with chord names inside each measure. A held chord is repeated in every measure it occupies. Chord changes retain their timing within the bar; there is no timestamp list in place of a chart. Prelude and incomplete final measures are marked separately.

A coherent saved beat/downbeat grid is used directly. If it is inconsistent, a constant-tempo grid is estimated from the displayed BPM, the quarter-note time signature and the first detected downbeat. This approximation is explicitly labelled in the PDF and may drift on variable-tempo recordings. Without a usable signature, tempo or downbeat origin, export asks for those indications instead of producing a timestamp list. No analysis runs and source events remain unchanged. The separate dictionary shows each selected fingering once. The filename ends in _chart.pdf.

## Piano: two hands
The keyboard proposes a piano voicing: the bass or root is blue for the left hand; the complete chord in the medium register is gold for the right hand. This realizes the recognized harmony; it does not identify the hands actually played in the recording. Transposition applies to both hands. String-instrument tuning and capo do not alter this voicing. The piano speaker uses these same notes. The string speaker instead uses the displayed fingering. Neither button changes the document or the sound settings saved for existing exports. This representation does not change MIDI export.



After the final word ends, the completed phrase disappears during instrumental passages, without an absence message. Its visibility follows the actual end of the last timed word, or the phrase end when word times are unavailable. Display anticipation does not advance this disappearance. The next phrase appears at its own time; chords remain visible.
