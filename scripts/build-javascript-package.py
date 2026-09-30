#!/usr/bin/env python3
"""Assemble only previously built/attested bytes; never run candidate code."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tarfile
import tempfile

root = Path(__file__).resolve().parents[1]
version = (root / 'VERSION').read_text().strip()
dist = Path(sys.argv[1] if len(sys.argv) > 1 else root / 'dist').resolve()
targets = {'linux-x86_64': 'linux-x64', 'linux-arm64': 'linux-arm64', 'macos-arm64': 'darwin-arm64', 'wasm32': None}
with tempfile.TemporaryDirectory(prefix='maelys-javascript-package-') as temporary:
    stage = Path(temporary)
    seen = {}
    expected_commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    for target, platform in targets.items():
        filename = f'maelys-datalog-{version}-javascript-{target}.tar.gz'
        archive_path = dist / filename
        receipt = json.loads((dist / f'release-receipt-{target}.json').read_text())
        assert receipt['commit'] == expected_commit, f'{target}: build commit differs from assembly checkout'
        assert receipt['version'] == version and receipt['tag'] == f'v{version}' and receipt['target'] == target
        entries = [a for a in receipt['artifacts'] if a['file'] == filename]
        assert len(entries) == 1 and hashlib.sha256(archive_path.read_bytes()).hexdigest() == entries[0]['sha256'], f'{target}: receipt mismatch'
        names = set()
        with tarfile.open(archive_path) as archive:
            for member in archive:
                path = PurePosixPath(member.name)
                assert not path.is_absolute() and '..' not in path.parts, 'unsafe archive path'
                if member.isdir():
                    continue
                assert member.isfile(), 'non-file member refused'
                name = str(path)
                assert name not in names, f'duplicate archive member: {name}'
                names.add(name)
                if name.startswith('prebuilds/'):
                    assert platform and name.startswith(f'prebuilds/{platform}/'), f'{target}: wrong native payload'
                if name.startswith('wasm/'):
                    assert platform is None, f'{target}: unexpected WASM payload'
                data = archive.extractfile(member).read()
                if name in seen:
                    assert data == seen[name], f'common package bytes disagree across targets: {name}'
                else:
                    seen[name] = data
                    output = stage / name
                    output.parent.mkdir(parents=True, exist_ok=True)
                    output.write_bytes(data)
        for profile in ('small', 'large'):
            required = [f'prebuilds/{platform}/{profile}.node'] if platform else [f'wasm/{profile}/engine.mjs', f'wasm/{profile}/engine.wasm']
            assert all(n in names for n in required), f'{target}: incomplete payload'
    package = json.loads((stage / 'package.json').read_text())
    assert package['name'] == '@maelys-dev/datalog' and package['version'] == version
    assert not package.get('scripts'), 'published package must not execute install/pack hooks'
    subprocess.run(['npm', 'pack', '--ignore-scripts', '--pack-destination', str(dist)], cwd=stage, check=True)
