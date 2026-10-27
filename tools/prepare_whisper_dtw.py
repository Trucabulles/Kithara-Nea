"""Prepare a pinned, isolated whisper.cpp build exposing complete DTW boundaries.

The upstream CLI and DLLs distributed with the application are not overwritten.
"""
from pathlib import Path
import hashlib
import urllib.request
import zipfile
import os

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('KITHARA_WHISPER_BUILD_ROOT', ROOT / 'build' / 'whisper-dtw'))
BUILD.mkdir(parents=True, exist_ok=True)
ARCHIVE = BUILD / 'whisper-v1.9.1.zip'
if not ARCHIVE.exists():
    urllib.request.urlretrieve('https://codeload.github.com/ggml-org/whisper.cpp/zip/refs/tags/v1.9.1', ARCHIVE)
assert hashlib.sha256(ARCHIVE.read_bytes()).hexdigest() == '98a57a88ef0e733b746544f8ea25157d3265fbf0dac5c32dbb527e6ef4dbfaac'
with zipfile.ZipFile(ARCHIVE) as archive:
    for entry in archive.infolist():
        assert (BUILD / entry.filename).resolve().is_relative_to(BUILD.resolve())
    archive.extractall(BUILD)
SOURCE = BUILD / 'whisper.cpp-1.9.1'

def replace(path, old, new):
    file = SOURCE / path
    text = file.read_text(encoding='utf8')
    assert text.count(old) == 1, (path, 'unexpected upstream source')
    file.write_text(text.replace(old, new), encoding='utf8')

replace('include/whisper.h', '        float vlen;        // voice length of the token', '''        float vlen;        // voice length of the token
        // KITHARA NEA experimental path boundaries, in upstream 10 ms units.
        int64_t t_dtw_start;
        int64_t t_dtw_end;''')

replace('src/whisper.cpp', '    // Place timestamps on segments\n', '''    // KITHARA NEA: preserve every DTW row transition, including the first.
    // Decoder attention row i predicts text token i; the last row supplies its end.
    std::vector<int64_t> boundaries((size_t) w->ne[0], -1);
    for (int i = 0; i < alignment->ne[1]; ++i) {
        const int row = whisper_get_i32_nd(alignment, 0, i, 0, 0);
        const int frame = whisper_get_i32_nd(alignment, 1, i, 0, 0);
        if (row >= 0 && row < (int) boundaries.size() && frame >= 0 && boundaries[row] < 0)
            boundaries[row] = seek + frame * 2;
    }
    size_t text_index = 0;
    for (size_t segment_index = i_segment; segment_index < i_segment + n_segments; ++segment_index) {
        for (auto & token : state->result_all[segment_index].tokens) {
            token.t_dtw_start = token.t_dtw_end = -1;
            if (token.id >= whisper_token_eot(ctx))
                continue;
            if (text_index + 1 < boundaries.size()) {
                token.t_dtw_start = boundaries[text_index];
                token.t_dtw_end = boundaries[text_index + 1];
            }
            ++text_index;
        }
    }

    // Place timestamps on segments
''')

replace('examples/cli/cli.cpp', '                                value_f("t_dtw", mt.data.t_dtw, true);', '''                                value_f("t_dtw", mt.data.t_dtw, params.dtw.empty());
                                if (!params.dtw.empty()) {
                                    value_f("t_dtw_start", mt.data.t_dtw_start, false);
                                    value_f("t_dtw_end", mt.data.t_dtw_end, true);
                                }''')
replace('examples/cli/cli.cpp', '                                m.text += whisper_token_to_str(ctx, tok_next.id);', '''                                m.text += whisper_token_to_str(ctx, tok_next.id);
                                // Preserve the final path boundary when UTF-8 token fragments merge.
                                if (!params.dtw.empty())
                                    m.data.t_dtw_end = tok_next.t_dtw_end;''')
replace('examples/cli/cli.cpp', '    ggml_backend_load_all();',
        '    // Static CPU pilot: do not discover DLL backends from another whisper build.')
# Diagnostic file names do not need to expose the developer's build directory.
# Replace only the predefined diagnostic token, not model or timing operations.
import re
for file in SOURCE.rglob('*'):
    if file.suffix not in ('.c', '.cpp', '.h', '.hpp') or not file.is_file():
        continue
    value = file.read_text(encoding='utf8')
    if '__FILE__' in value:
        value = re.sub(r'\b__FILE__\b', '"'+file.name+'"', value)
        file.write_text(value, encoding='utf8')
print(SOURCE)
