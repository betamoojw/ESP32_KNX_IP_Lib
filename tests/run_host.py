"""Compile and execute the production C++ protocol tests with GCC, Clang, or Zig.
Examples: python tests/run_host.py --compiler g++
          python tests/run_host.py --compiler .tools/ziglang/zig.exe
"""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--compiler', default='g++')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
build = root / 'build' / 'host'
build.mkdir(parents=True, exist_ok=True)
os.environ.setdefault('ZIG_GLOBAL_CACHE_DIR', str(build / 'zig-cache'))
compiler = [args.compiler]
if Path(args.compiler).stem == 'zig':
    if args.sanitize:
        parser.error('Use GCC or Clang with sanitizer runtimes for --sanitize; Zig runtime instrumentation is not verified by this runner.')
    compiler += ['c++', '-nostdlib++']
flags = ['-std=c++11', '-Wall', '-Wextra', '-Werror', '-g', '-I.']
if args.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
output = build / ('test_protocol.exe' if os.name == 'nt' else 'test_protocol')
subprocess.run(compiler + flags + ['knx-codec.cpp', 'knx-protocol.cpp', 'tests/test_protocol.cpp', '-o', str(output)], cwd=root, check=True)
subprocess.run([str(output)], cwd=root, check=True)
output = build / ('test_client.exe' if os.name == 'nt' else 'test_client')
sources = ['knx-codec.cpp', 'knx-protocol.cpp', 'esp-knx-ip.cpp', 'esp-knx-ip-client.cpp',
           'esp-knx-ip-send.cpp', 'esp-knx-ip-conversion.cpp', 'esp-knx-ip-config.cpp', 'tests/test_client.cpp']
subprocess.run(compiler + flags + ['-DESP32', '-Itests/stubs'] + sources + ['-o', str(output)], cwd=root, check=True)
subprocess.run([str(output)], cwd=root, check=True)
