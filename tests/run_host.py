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
sources = ['knx-codec.cpp', 'knx-protocol.cpp', 'esp-knx-ip.cpp', 'esp-knx-ip-client.cpp',
           'esp-knx-ip-send.cpp', 'esp-knx-ip-conversion.cpp', 'esp-knx-ip-config.cpp', 'tests/test_client.cpp']
for defines, diagnostic in [
        (['-DMAX_CALLBACK_ASSIGNMENTS=255'], 'EEPROM_SIZE is too small'),
        (['-DMAX_CALLBACKS=256'], 'MAX_CALLBACKS must be in 1..255')]:
    result = subprocess.run(compiler + flags + ['-DESP32', '-Itests/stubs'] + defines +
                            ['-x', 'c++', '-c', '-', '-o', str(build / 'invalid_capacity.o')], input='#include "esp-knx-ip.h"\n',
                            cwd=root, text=True, capture_output=True)
    if result.returncode == 0 or diagnostic not in result.stderr:
        raise RuntimeError('Missing capacity validation: ' + diagnostic + '\n' + result.stderr)
print('PASS: invalid capacity configurations rejected', flush=True)
for target in ['ESP32', 'ESP8266']:
    for profile, defines in [('default', []), ('capacity255', [
            '-DMAX_CALLBACK_ASSIGNMENTS=255', '-DMAX_CALLBACKS=255', '-DEEPROM_SIZE=2048'])]:
        output = build / ('test_client_' + target.lower() + '_' + profile + ('.exe' if os.name == 'nt' else ''))
        subprocess.run(compiler + flags + ['-D' + target, '-Itests/stubs'] + defines + sources + ['-o', str(output)], cwd=root, check=True)
        subprocess.run([str(output)], cwd=root, check=True)
output = build / ('test_example.exe' if os.name == 'nt' else 'test_example')
example_compiler = [arg for arg in compiler if arg != '-nostdlib++']
subprocess.run(example_compiler + flags + ['-DESP32', '-Itests/stubs'] + sources[:-1] +
               ['tests/test_example.cpp', '-o', str(output)], cwd=root, check=True)
subprocess.run([str(output)], cwd=root, check=True)
