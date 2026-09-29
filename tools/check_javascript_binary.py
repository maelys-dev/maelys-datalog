#!/usr/bin/env python3
"""Fail packaging if a native addon's exports or OS requirements drift."""
import json
from pathlib import Path
import platform
import re
import subprocess
import sys

POLICY = json.loads(Path(__file__).with_name('javascript-build.json').read_text())
EXPORTS = {'napi_register_module_v1', 'node_api_module_get_api_version_v1'}


def output(*args):
    return subprocess.check_output(args, text=True)


def version(value):
    parts = tuple(map(int, value.split('.')))
    return parts + (0,) * (3 - len(parts))


def check(path):
    if platform.system() == 'Darwin':
        symbols = set(output('nm', '-gUj', path).split())
        assert symbols == {'_' + name for name in EXPORTS}, f'unexpected exports: {symbols}'
        commands = output('otool', '-l', path)
        minimum = re.findall(r'^\s*minos ([0-9.]+)$', commands, re.M)
        assert len(minimum) == 1, 'missing or ambiguous Mach-O deployment target'
        assert version(minimum[0]) == version(POLICY['macosMinimum']), f'macOS target drift: {minimum}'
        # strip -S -x must leave no local symbols or debug records.
        # Apple's linker may retain this absolute loader marker after strip -x.
        locals_ = set(output('nm', '-aj', path).split()) - set(output('nm', '-gj', path).split())
        assert locals_ <= {'radr://5614542'} and '__debug_' not in commands, 'unstripped local/debug symbols'
        requirement = 'macOS ' + minimum[0]
    else:
        symbols = {line.split()[-1] for line in output('nm', '-D', '--defined-only', path).splitlines()}
        assert symbols == EXPORTS, f'unexpected exports: {symbols}'
        sections = output('readelf', '-SW', path)
        assert '.symtab' not in sections and '.debug_' not in sections, 'unstripped ELF'
        versions = set(re.findall(r'Name: (\S+)', output('readelf', '--version-info', path)))
        assert versions and all(re.fullmatch(r'GLIBC_[0-9.]+', v) for v in versions), f'unsupported ABI requirements: {versions}'
        maximum = max(versions, key=lambda v: version(v.removeprefix('GLIBC_')))
        assert version(maximum.removeprefix('GLIBC_')) <= version(POLICY['glibcMaximum']), f'glibc floor exceeds policy: {maximum}'
        needed = re.findall(r'Shared library: \[(.*?)\]', output('readelf', '-d', path))
        allowed = {'libc.so.6', 'libm.so.6', 'libpthread.so.0', 'libdl.so.2', 'librt.so.1', 'ld-linux-x86-64.so.2', 'ld-linux-aarch64.so.1'}
        assert set(needed) <= allowed, f'unexpected runtime dependencies: {needed}'
        requirement = maximum
    print(f'{path}: exactly two Node-API exports; stripped; {requirement}')


if __name__ == '__main__':
    check(sys.argv[1])
