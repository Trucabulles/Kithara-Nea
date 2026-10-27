"""Build the existing DTW decoder with an isolated, static Vulkan backend.

Requires CMake, MSVC and a Vulkan SDK (glslc, headers and import library).
The SDK is a build dependency only and is never copied into the application.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--sdk', default=os.environ.get('VULKAN_SDK'))
parser.add_argument('--cmake', default='cmake')
args = parser.parse_args()
if not args.sdk:
    parser.error('Set VULKAN_SDK or pass --sdk')
build = ROOT / 'build' / 'whisper-vulkan'
environment = dict(os.environ, KITHARA_WHISPER_BUILD_ROOT=str(build), VULKAN_SDK=str(Path(args.sdk).resolve()))
subprocess.run([sys.executable, str(ROOT / 'tools/prepare_whisper_dtw.py')], env=environment, check=True)
source = build / 'whisper.cpp-1.9.1'
cli = source / 'examples/cli/cli.cpp'
text = cli.read_text(encoding='utf8')
anchor = '    // Static CPU pilot: do not discover DLL backends from another whisper build.'
assert text.count(anchor) == 1
text = text.replace(anchor, '''    // Built-in backends only: never load unrelated backend DLLs.
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    if (argc == 2 && std::strcmp(argv[1], "--kithara-gpu-probe") == 0) {
        int ordinal = 0;
        for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
            auto device = ggml_backend_dev_get(i);
            auto type = ggml_backend_dev_type(device);
            if (type != GGML_BACKEND_DEVICE_TYPE_GPU && type != GGML_BACKEND_DEVICE_TYPE_IGPU)
                continue;
            auto backend = ggml_backend_dev_init(device, nullptr);
            if (backend) {
                std::printf("KITHARA_GPU_READY=%d\\n", ordinal);
                std::printf("KITHARA_GPU_NAME=%s\\n", ggml_backend_dev_description(device));
                ggml_backend_free(backend);
                return 0;
            }
            ++ordinal;
        }
        return 3;
    }''')
cli.write_text(text, encoding='utf8')
subprocess.run([args.cmake, '-S', str(source), '-B', str(build / 'compiled'), '-A', 'x64',
                '-DGGML_VULKAN=ON', '-DBUILD_SHARED_LIBS=OFF', '-DGGML_BACKEND_DL=OFF',
                '-DGGML_NATIVE=OFF', '-DGGML_AVX2=ON', '-DWHISPER_BUILD_TESTS=OFF',
                '-DCMAKE_CXX_FLAGS=/utf-8', '-DCMAKE_C_FLAGS=/utf-8'], env=environment, check=True)
subprocess.run([args.cmake, '--build', str(build / 'compiled'), '--config', 'Release',
                '--target', 'whisper-cli', '--parallel', '8'], env=environment, check=True)
